# Apple IIgs SHR UI Playbook — building high-res dashboards

Hard-won learnings from the GS Miner visualization. This is **toolchain-agnostic
and app-agnostic** — read it before building any Super Hi-Res UI on the GS so we
don't re-walk the painful, tedious (but important) visual iteration.

> TL;DR architecture: **a static SHR "frame" plate + everything dynamic drawn by
> code on top.** Build the frame *procedurally at native 320×200* (not AI-then-
> downscale). Stay in **320 mode** for a color UI (640 trades all color for crisp
> text — only worth it for greyscale tools, see §10). Metal = smooth gradients
> snapped to **per-zone neutral grey ramps, no dithering**, with **high-contrast
> chiaroscuro bevels** for 3D depth. All text = a **hard-edged bitmap font drawn
> by code**, never baked. **Labels are black on bright nameplates; values are
> LED-green in black screens — never grey-text-on-bare-metal** (§5b). Share one
> **palette contract** (16-zone hybrid, §3) between the frame converter and the C
> draw code.

---

## 1. The core architecture: frame plate + code-drawn content

Split the screen into two layers:

1. **Static frame plate** — the chassis: metal, bevels, screws, recessed wells
   (the empty black/blue boxes), axis chrome. Converted once to an SHR blob and
   loaded at runtime.
2. **Dynamic content** — labels, values, the scope/waveform, VU bars, status
   lamps, button fills, the coin/logo. **Drawn by C every frame** into the wells.

Why this split (all four reasons earned the hard way):

- **Sharpness.** Anti-aliased text/graphics baked into an image and downscaled to
  320×200 turn to mush. Code-drawn hard-edged pixels stay razor sharp.
- **Palette control.** You decide exactly which 4-bit index each pixel uses, so
  colors are stable and predictable (see the palette contract, §3).
- **Iteration speed.** Re-skin the chassis without recompiling C; change a label
  or value without re-exporting an image.
- **Liveliness for free.** Wells are flat color; code owns them, so animation,
  palette-cycling, and state changes (lit/dim button, green/red lamp) are cheap.

---

## 2. Build the frame PROCEDURALLY at native resolution

We tried three ways to make the chassis. Only the last one worked well:

| Approach | Result |
|---|---|
| AI image → `png2shr` downscale/quantize | Pretty source, but **pixelated** after downscale; muddy text; palette speckle. |
| AI image, iterate prompts for "empty label strips" | The model **kept collapsing** two-zone boxes into one black inset — no room for labels. Unreliable. |
| **Procedural render at 320×200** (`viz/build_frame.py`) | **Win.** Pixel-exact layout, smooth metal, trivial to tweak, exact known coordinates. |

Key insight: at native 320×200 there is **no downscaling step**, so every pixel
is intentional. The builder *is* the source of truth for all layout coordinates
(the C code reuses the same numbers — no "measure the wells" guessing).

`build_frame.py` primitives worth reusing:
- `vgrad` (vertical gradient), `sheen` (gaussian highlight band, tunable amp) —
  metal faces.
- `bevel` (1px 2-tone), `bevel2` (**2px chiseled, high-contrast** — the high-relief look),
  `chamf` (1px corner chamfer) — refined, non-boxy edges.
- `rtile` (raised cell), `recess` (cut-in well), `groove` (engraved group outline),
  `plate` (**bright near-white nameplate strip for black labels**, §5b),
  `screw` (corner fastener).
- **Don't pre-snap metal to a fixed grey set in the builder** — emit continuous
  gradients and let the converter's per-zone ramp (§3) do the quantization. We
  had a bug where `build_frame` pre-snapped to 13 greys and flattened everything.

---

## 3. The palette contract — the 16-ZONE HYBRID (v3)

SHR 320 mode gives **16 colors per scanline**, each line picking one of 16
palettes via its SCB. **The single biggest "8-bit vs. 16-bit" lever is how you
spend those 16 palettes.**

- **v2 (wrong): 4 band palettes.** One palette for the whole header, one for the
  whole mid section, etc. → the entire band is literally only 16 colors → flat,
  "lost all the detail," screams 16-color. This looked amateurish.
- **v3 (right): 16 ZONES stacked down the screen.** Cut the panel into 16
  horizontal zones (we use 3 header + 1 wallet + 8 mid + 4 bottom). Each zone
  gets its **own** palette. Within each zone:
  - a few **band-fixed INK indices** hold constant accent colors (so the C code
    can draw and always know "index 11 = cyan" anywhere in that band);
  - the **remaining indices hold a grey ramp recomputed from THAT zone's local
    metal gradient** → ~10 fresh greys per ~12 lines → *hundreds* of distinct
    greys down the panel → smooth metal + crisp bevels, while ink stays
    code-addressable.

This is the whole trick: ink is global-per-band (predictable for code), greys are
local-per-zone (rich gradients). See `viz/spec.py` (`build_zone_palettes`, `ZONES`,
`BANDS`).

Ramp construction that matters: **reserve the zone's true min/max luminance as
the ramp endpoints** (so high-contrast bevel shadow + specular highlight survive)
and **pack the middle by equal-population quantiles** (so the broad metal faces
get the most steps and band invisibly). `np.percentile(m, linspace(14,86,n-2))`
plus the true min/max.

Ink layout we use (mirrored in `miner/contract.h`):
- all: `C_BLACK=0  C_WHITE=15`
- header: `H_BLUE=10 H_CYAN=11 H_GREEN=12 H_RED=13 H_GOLD=14`
- wallet: `W_BLUE=11 W_CYAN=12`
- mid: `M_LED=8  M_RB0=9` (rainbow 9..14)
- bottom: `B_AMBER=11 B_VUG=12 B_VUY=13 B_VUR=14` (hash green = `B_VUG`)

Still true regardless of zone count:
- **Color indices are band-relative** — draw with the constant for the band the
  pixel lands in. The same index 11 is cyan in the header, blue in the wallet,
  rainbow-2 in mid, amber in the bottom. That's by design; just use the right name.
- **Watch band-crossing graphics.** A gold coin (index 14) that spills from header
  into wallet renders wrong there. Keep a graphic inside one band, or put its
  color at the same index in both palettes.

Files: `viz/spec.py` (contract + zones + band ink map), `gen_contract_c.py` →
`miner/contract.h`, and `frame2shr_contract.py` which quantizes the frame
**against the fixed contract** (not auto-generated palettes).

---

## 4. Smooth metal = clean contiguous greys + HIGH-CONTRAST bevels, NOT dithering

Two levers, both counterintuitive:

**(a) No dithering on metal.** Dithering a near-flat panel adds scattered greys
that read as cheap/8-bit — the opposite of the goal. The "smooth as silk" look
comes from **smooth gradients snapped to the per-zone grey ramp** (§3) → clean,
contiguous bands. No bayer, no error diffusion on the metal. Keep greys
**neutral** — *don't* mix slightly-different-hued greys (we tried a "steel tint";
it looked worse — reverted). (Dithering still has a place: photographic gradients,
the scope rainbow — just not brushed metal.)

**(b) Crank the bevel contrast (chiaroscuro).** A subtle bevel looks like a flat
render. Convincing metal is *high contrast*: near-white specular highlights against
near-black shadow on the same edge. Use **2px chiseled bevels** (`bevel2`),
**bright outer rims**, and **deep recesses** for the wells. The per-zone ramp
preserving true min/max (§3) is what lets those extreme edge tones survive
quantization. Dimension comes from **strong** bevels + clean faces — the steps
in between band invisibly because the ramp is dense there.

> Lesson the hard way: when the frame looked "8-bit amateurish," the fix was NOT
> more colors or dithering — it was (1) 16 zones not 4, and (2) more bevel
> contrast. The color *count* per line never changed; the *spend* did.

---

## 5. Text: a hard-edged bitmap font, drawn by code

- Use a **4×6 hard-edged pixel font** (`viz/font4x6.py`), uppercase + digits +
  symbols. No anti-aliasing → survives the 4-bit palette perfectly.
  - Font-size journey: 5×7 read **chunky/oversized** on the dense chassis; 3×5 was
    **illegible** (ambiguous glyphs). **4×6 is the balance** — small enough to feel
    like crisp instrument labels, big enough to read. Pick the font *after* the chassis density
    is set, not before.
- **Generate the C glyph table from the same Python source** (`gen_font_c.py` →
  `miner/font_gs.h`) so the Mac proof and the GS hardware font are byte-identical.
  Pack rows width-agnostically (bit `GLYPH_W-1` = leftmost px) so swapping font
  size only changes `GLYPH_W/H`, not the draw code.
- **A second, case-sensitive font for mixed-case strings.** The 4×6 chassis font is
  uppercase-only. A Bitcoin wallet (`3CfSNGtkdp…`) is **case-significant**, so we
  ship a separate **5×7 mixed-case "wallet" font** (`miner/font_w_gs.h`,
  `wallet_text()`) used for the wallet value on both the main panel and config page.
  Lesson: pick the font *per field's needs* — uppercase labels can be tiny, but any
  case-significant value (wallet, base58/bech32) needs a lower-case-capable face.
- **Never bake text into the frame image.** Static labels feel tempting (they
  never change) but still muddy on conversion. Draw them in code.
- Reserve **label real-estate in the frame** (see §5b for the nameplate treatment): the
  frame-without-text must *keep* the nameplates/screens — the #1 layout bug was
  empty boxes collapsing and leaving nowhere for labels.

---

## 5b. The text treatment: nameplates + LED screens (NOT grey-on-metal)

The frame can be gorgeous and still "blow up" the moment you add text — because
the text, not the metal, is what fails. The secret is **where** text sits:

- **Labels = solid black on a BRIGHT nameplate.** Build a near-white plate strip
  (`plate()` in `build_frame.py`) into the chassis and draw the label as **pure
  black** (index 0) on it. Maximum contrast, razor clear. This is how crisp labels
  read so cleanly.
- **Values = LED-green on a BLACK screen.** Numeric readouts go in a recessed
  black well and are drawn in the LED-green ink (`M_LED`). Reads as a real LED
  segment display.
- **Never** put a label as grey/light text directly on mid-grey metal — low
  contrast, muddy, "8-bit." And **never** stuff the label *inside* the black LED
  screen next to the value — it jams up and kills the LED illusion.
- **No fake shadow/emboss on black labels.** We tried a white highlight under the
  black text ("engraved") — it just looked busy. Solid black is clearer.
- LED green: tune the shade **slightly dark** (`LED=(1,11,2)`) so it reads as a
  lit segment, not neon.

So each readout cell = **black LED screen (green value) on top + bright nameplate
(black label) below**, baked as two zones in the frame; code only draws the
green value and the black label into them.

---

## 6. GS drawing primitives (C)

- SHR pixel write (320 mode, 2 px/byte, hi nibble = left):
  ```c
  p = 0xE12000 + y*160 + (x>>1);
  if (x & 1) *p = (*p & 0xF0) | (c & 0x0F);
  else       *p = (*p & 0x0F) | ((c & 0x0F) << 4);
  ```
- Install plate: palettes → `$E19E00`, SCBs → `$E19D00`, pixels → `$E12000`;
  `*$C029 |= 0x80` for SHR/320.
- Save/restore the desktop ($2000..$9FFF) around the panel so you can return to
  the Finder cleanly.
- Color indices are **band-relative** — always draw with the constant for the
  band the pixel lands in.
- Blob layout we use: `[32000 pixels][200 SCBs][512 palettes]` = 32712 bytes; the
  loader (`miner/viz.c`) reads it straight to the SHR registers.

---

## 7. Pitfalls log

- **4 band palettes = flat 8-bit** — the band is then literally 16 colors. Use 16
  ZONES (§3); it's the difference between "amateurish" and "pro."
- **Grey/light text on bare metal** — muddy, low-contrast. Black-on-bright-plate
  for labels, green-on-black-screen for values (§5b).
- **Steel-tinted greys** — mixing hued greys reads worse than neutral; we reverted.
- **Pre-snapping metal in the builder** — flattens the gradient before the zone
  ramp can do its job. Emit continuous tones; quantize once, in the converter.
- **Empty-frame box collapse** — removing baked labels let the black insets eat
  the grey label strip → no room for code labels. Keep the two-zone structure.
- **AI won't reliably leave empty label strips** — don't fight it; go procedural.
- **Downscaling muddles** text, logos, coins, fine textures → code-draw them.
- **Dithering flat metal = noise** → snap to a clean neutral ramp instead (§4).
- **Palette speckle** from per-row palette flip + tinted greys → contiguous band
  palettes + shared neutral grey ramp.
- **Band-crossing color** (gold coin into the wallet band) → keep graphics in one
  band or share the color across both palettes.
- **Corner screws overwriting wells** → inset the nearest box so the screw sits in
  clean metal (or deliberately place the screw *on* a box and accept it).
- **Screws drifting into the bezel** → keep them at a consistent inset from the
  *inner border* (we use `x=9/310`), not jammed against the outer bevel (`x≈314`,
  `y≈196` lands *on* the 2px frame border and looks detached). Mirror top/bottom at
  the same x; put the bottom row just *below* the last content row, not in the rim.
- **Runtime draws covering baked chrome** → if code draws buttons/values over a
  region that also holds a baked screw (or any chassis detail), the dynamic draw
  wins and the detail vanishes. Fix: **re-draw that chrome in code *after* the
  buttons** (we added `cfg_draw_screws()` as the last call in the config's static
  draw). Don't rely on the baked plate alone where live widgets overlap.
- **Touching a page you weren't asked to** → the main panel and config page share
  primitives but are *independent* layouts. A "tidy both" coordinate change to the
  shared screw loop silently moved the **main** page's screws too. Change one page's
  geometry at a time; keep their coordinate sets separate even when the code rhymes.
- **320 px is cramped** — labels beside lamps (`MINING`/`POOL OK`) barely fit;
  budget horizontal space early; abbreviate or relocate labels in code.
- **MAME/Ample disk persistence** — eject before host-side inject; full relaunch
  to see hard-disk changes; fresh filename dodges the Finder desktop cache.

---

## 8. The visual-iteration discipline

- Always review a **2× nearest-neighbor preview** (`*_2x.png`) — judge it as
  pixels, not a smooth photo.
- **Lock elements explicitly** and change **one thing at a time**; regenerate and
  re-look. (We did ~10 frame revisions this way; it's tedious but converges.)
- Keep a **Mac-side full render-proof** (`render_panel.py`) that draws the *whole*
  populated panel through the contract — validates the entire look (and the
  16-colors/line budget) **before** spending slow GS build/inject cycles.
- Prove the **code-draw pipeline on hardware early** with sample/static values;
  wire live data only after the look is right.

---

## 9. Modal editor screens (the config page pattern)

A settings/config page is just **a second frame plate + a modal draw/input loop**
on top of the same pipeline. What we learned building the GS Miner CONFIG page:

**Reuse the whole pipeline for a second chassis.** `build_config.py` (procedural
chassis) → `config_frame.png` → `config2shr.py` (→ `config.shr`), with a Mac proof
`render_config.py`. Same primitives, same palette contract, same font. Don't invent
a new path for "just a settings screen."

**Editable text fields in SHR (no TextEdit).** Each field = a recessed LCD well +
a small field model in C: `{buffer, max_chars, well_rect}`. The modal loop:
- **click-to-focus** (hit-test the wells), **TAB** cycles focus, **typing** appends
  if `len < max`, **DELETE/BS** erases, **RETURN** = save+exit, **ESC** = quit.
- Draw a 1px **caret** after the text when focused; redraw only the focused field on
  each keystroke (region-blit, not full repaint) to stay flicker-free.
- Validate on SAVE (IP dotted-quad, port range, base58/bech32 wallet, non-empty
  worker); on failure, **flag the offending field red** + show the reason in the
  title bar, and refocus it.

**A mode/enum setting = a labeled button pair, not a typed field.** We first made
"DEMO/LIVE" a typed text field — users didn't notice it was editable. Replacing it
with a **LIVE | DEMO button pair** (the active one drawn *pressed*, colors blue/
amber) made it obvious and click-only. Mirror an existing affordance (here the main
panel's RUN | STOP pair) so it reads as "a control," not "a label."

**Button stack discipline.** Align the button column with the help/content panel
(same top and bottom y), use a uniform pitch (`btn_h + gap`), and order by
frequency/severity (SAVE → DEFAULTS → mode → CANCEL·QUIT). Aligning the stack to the
panel edges is what makes it look designed rather than stacked.

**Persist to a sidecar file.** `MINER.CONF`, one field per line, loaded on boot,
written on SAVE; a DEFAULTS button restores compiled defaults. Handle legacy/short
files gracefully (prepend a default for any newly-added leading field). **Gotcha:**
don't truncate each line to its max *while reading* — that clips fields whose buffer
is smaller than the data you mean to keep elsewhere; truncate only when copying into
the specific field's buffer.

**Coordinates live in three places — keep them in sync.** `build_config.py`
(chassis), `render_config.py` (Mac proof), and `viz.c`/`contract_cfg.h` (GS draw +
hit-test) must agree on every well/button rect. A change in one without the others
means the click target and the drawn box drift apart. Treat the builder as source of
truth and mirror its numbers into the C `#define`s.

**Status/error surfacing.** Reuse on-canvas chrome for state: a title-bar string
(red on error), a coin/status badge that swaps text+color (`DEMO MODE` gold /
`SHA-256D` cyan / `NO TCP`·`NO IP` red), and a status lamp (grey/red/green). Re-check
state each loop pass but **only redraw on change** (cache last state) so you're not
repainting every frame.

---

## 10. Decision log: 320 mode vs 640 mode

The temptation: 640 mode is 2× horizontal resolution → crisper text (it matches
how sharp classic demo text looks). The catch: **640 mode is only ~4 colors per pixel**
(positional palette groups), so it leans on **fine dithering** for every tone and
hue. We built a faithful proof (`viz/render_640.py`: 640-wide, 4 colors/line +
ordered dither) and compared side-by-side with the 320 panel.

- **640 win:** noticeably crisper, finer text.
- **640 cost:** the metal goes **noisy/flat** (black/white/grey dither), and rich
  accent colors (the scope rainbow, blue LCDs, gold) collapse.

**Verdict:** **320 for a color UI** like this miner — rich, smooth metal + vibrant
accents beat marginally crisper text. **640 only for a greyscale tool** (think
a tracker-style tool) where text density matters more than color. Decide this *first*;
it dictates the whole palette strategy.

---

## 11. Pipeline / file map (GS Miner viz)

| File | Role |
|---|---|
| `viz/build_frame.py` | **Procedural chassis** at 320×200; source of truth for all layout coords. Emits *continuous* gradients. → `frame_proc.png`. |
| `viz/spec.py` | Palette contract **v3** (16 zones, band ink map, per-zone grey ramps) + `build_zone_palettes` + `quantize`. |
| `viz/frame2shr_contract.py` | Quantize the frame against the fixed contract → `frame.shr` SHR blob. |
| `viz/font4x6.py` | Hard-edged 4×6 font (Mac proof + glyph source of truth). |
| `viz/font5x7w.py` | 5×7 **case-sensitive** wallet font (mixed-case values). |
| `viz/gen_font_c.py` | Emit `miner/font_gs.h` (+ wallet `font_w_gs.h`) from glyph data. |
| `viz/gen_contract_c.py` | Emit `miner/contract.h` / `contract_cfg.h` ink indices from `spec.py`. |
| `viz/render_panel.py` | Mac full-panel render-proof (sanity-check the whole look). **Mirror any coord/label change into `miner/viz.c`.** |
| `viz/render_640.py` | Side experiment: faithful 640-mode proof (see §10). |
| `viz/build_config.py` | **Procedural CONFIG chassis** (the second plate); source of truth for config coords. → `config_frame.png`. |
| `viz/render_config.py` | Mac proof of the populated CONFIG page (`config_full2x.png`). |
| `viz/config2shr.py` | Quantize the config frame → `config.shr`. |
| `miner/viz.c` | GS loader + draw primitives + **live dashboard + modal config editor**. |
| `miner/contract_cfg.h` | CONFIG-page ink indices + geometry `#define`s (mirror of `build_config.py`). |

Build/inject (both chassis + binary): build `frame.shr` **and** `config.shr`, then
`occ -O255 -w255 viz.c -L. -llib65816hash -o viz ; iix chtyp -t s16 viz`, then
`scripts/inject_gsminer.sh -f` — it reads `APPVER` from `viz.c`, injects as `GSMINE<NN>`
(S16), and refreshes `PANEL` (`frame.shr`) + `CONFIG` (`config.shr`). Keeps current
+ previous rev; `-f` allows injecting while Ample runs (eject/re-insert to see it).

> **Iteration economy (learned the slow way):** for pure-chrome tweaks (screw
> positions, button alignment, help text), judge the **Mac 2× proof first**
> (`render_config.py` → `config_full2x.png`) before spending a GS build/inject/boot
> cycle. Several screw/spacing rounds were burned changing coords blind; the proof
> catches "off in the bezel" / "covered by a button" in one look.

---

## 11. GS Miner shipping status (2026-06-03)

The miner dashboard from this playbook ships as **`GSMINE95` / V0.95** (`miner/viz.c`).

| Area | Status |
|------|--------|
| Frame + CONFIG chassis (`frame.shr`, `config.shr`) | ✅ locked (logo well widened 82→85) |
| Live data + DEMO/LIVE + CONFIG persistence | ✅ |
| Stratum LIVE (`stratum.c`) + `SYSFILES/` paths | ✅ |
| Custom Finder icon (`viz/build_icon.py`) | ✅ |
| In-app logo well — gold **GS** / red **MINER** + dynamic `v0.95` stamp | ✅ runtime SHR blit (`miner/logo_gs.h` + `draw_logo()`); see POC §16k |
| User docs | ✅ `README.md`, POC §16 |

**Porting a proof to a runtime blit (reusable pattern):** rasterise the art to **palette indices**
in the Mac proof (`logo_depth_proof.py` `render_well_idx()` → `miner/logo_gs.h`), reserve **255 =
transparent** so the chassis recess shows through, and blit non-transparent indices with `setpix()`.
Keep anything dynamic (version) as a separate `text_r()` stamp drawn *after* the blit. Header-band
palette has flat accents only (no dark-gold/red), so lean on the **black outline + grey bevel** for
depth. Logo notch: bottom-center on **GS** is a 4-pixel carve at the G\|S seam only; aggressive splits
broke the G glyph — keep the conservative notch.
