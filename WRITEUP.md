# GS Miner — Mining Bitcoin on a 1986 Apple IIGS

*The full story: background, architecture, the milestone-by-milestone build, the
revisions, and the war stories.*

> **Status:** beta — **`GSMINE95` / V0.95** (2026-06-03). **🏆 First confirmed LIVE mining on
> real Apple IIgs hardware** — a modded ROM 3 “Dark” IIgs (TransWarp GS @ 14 MHz, Uthernet II in
> slot 2, CFFA, 8 MB, VidHD) on `solo.ckpool.org`. **Measured ~21–22 H/s on the 14 MHz TWGS;
> ~9–10 H/s on a stock 2.8 MHz GS — measured, not estimated.** Mining core + Marinetti networking + a custom SHR
> dashboard proven end-to-end. The shippable disk is a 2IMG-wrapped, CFFA-mountable `GSMINER` volume.
>
> This document is the **narrative write-up** for the repo. The exhaustive lab notebook
> (every gotcha, every rev, every measurement) lives in
> [`IIGS_BITCOIN_MINER_POC.md`](IIGS_BITCOIN_MINER_POC.md); the reusable UI method is in
> [`IIgs_SHR_UI_PLAYBOOK.md`](IIgs_SHR_UI_PLAYBOOK.md).

![GS Miner live dashboard — mining on solo.ckpool.org](branding/gsminer_v095_dashboard.png)

![GS Miner CONFIG page — pool, wallet, and DEMO/LIVE toggle](branding/gsminer_v095_config.png)

---

## Table of contents

1. [TL;DR](#1-tldr)
2. [Background & motivation](#2-background--motivation)
3. [Is this *really* mining?](#3-is-this-really-mining)
4. [The stack](#4-the-stack)
5. [Architecture](#5-architecture)
6. [Deep dive: the SHA-256d core](#6-deep-dive-the-sha-256d-core)
7. [Deep dive: networking (Marinetti + Stratum)](#7-deep-dive-networking-marinetti--stratum)
8. [Deep dive: the SHR dashboard](#8-deep-dive-the-shr-dashboard)
9. [The toolchain](#9-the-toolchain)
10. [The build, milestone by milestone](#10-the-build-milestone-by-milestone)
11. [Revision history](#11-revision-history)
12. [War stories (the bugs that cost us days)](#12-war-stories-the-bugs-that-cost-us-days)
13. [Performance & the "turbo GS" lottery](#13-performance--the-turbo-gs-lottery)
14. [The in-app logo pipeline](#14-the-in-app-logo-pipeline)
15. [Build it yourself](#15-build-it-yourself)
16. [Repo layout](#16-repo-layout)
17. [Credits & references](#17-credits--references)
18. [Roadmap & optimization notes](#18-roadmap)

---

## 1. TL;DR

**GS Miner** is a native **GS/OS application** for the Apple IIGS that:

- connects to a **real Bitcoin Stratum pool** over **Marinetti TCP/IP** (Uthernet II / emulated),
- pulls live jobs (`mining.notify`), assembles real 80-byte block headers,
- runs **SHA-256d** on the 65816 with a hand-tuned assembly core + midstate optimization,
- and renders a live **Super Hi-Res dashboard**: hashrate, NET DIFF,
  block height, JOBS, a scrolling hash oscilloscope, VU history, and a BLOCK ETA measured
  in **quadrillions of years**.

It hashes **real work**. It just (almost) never finds a share, because a stock GS does
~10 hashes/second and the network wants ~10²² of them. That's the joke, and it's also
the point. **Not a retirement plan.**

---

## 2. Background & motivation

The Apple IIGS turns **40** in 2026. The machine shipped in 1986 with a 16-bit **65C816**
at a stock 2.8 MHz, **Super Hi-Res** graphics (320×200 / 640×200, 4096-color palette space),
the Ensoniq DOC sound chip, and a real GUI in **GS/OS**. With a **Uthernet II** card and the
**Marinetti** TCP/IP stack, it can talk to the modern internet.

The direct inspiration is Charles Mangin's Apple //e **[8BITCOIN](https://retroconnector.com/2019/08/13/mining-bitcoin-on-an-apple-ii-a-highly-impractical-guide/)**
(2019) — "a highly impractical guide" to mining on an 8-bit Apple II. GS Miner asks: what
does the *16-bit* successor do if you give it a real network card, a real pool, and a real
demoscene dashboard? The answer is a fully functional miner that is gloriously, hilariously
slow — and a nice vehicle for documenting GS/OS app development, 65816 crypto, Marinetti
networking, and bare-metal SHR graphics for the next person.

---

## 3. Is this *really* mining?

Yes. LIVE mode does the real thing: subscribe + authorize on a pool, parse live jobs,
build the header, and run SHA-256d over the nonce range. What it does **not** do is spam
the pool with shares it knows will be rejected.

- Each nonce: `sha256d_header()` → count **leading zero bits** (`zbits_of`).
- Submit **iff** `zbits >= strat_need_bits()`.
- On a **public pool**, `strat_need_bits()` is derived from the pool's `mining.set_difficulty`
  (diff ≥ 1 → ≥ **32 bits**; ckpool soaks observed ~**45 bits**). At ~10 H/s that target is
  unreachable in practice, so SHARES stays `0 / 0`.
- On a **private/LAN IP** (the `scripts/mock_pool.py` dev path) the target is overridden to an easy
  **8 bits**, so you can watch the full submit → **ACCEPT** handshake end-to-end.

There is **no** `if (public_pool) never_submit`. A hash that clears the bar always submits —
on stock iron, in a turbo emulator, or on future accelerated hardware. Filtering shares we
*know* will fail is hygiene (avoids reject floods and rate-limit/ban risk), **not** fake
mining. Full prose: README "Is this really mining?"; mechanism: POC §16i.

---

## 4. The stack

| Layer | What we use |
|---|---|
| CPU | WDC **65C816**, 16-bit, stock **2.8 MHz** (accelerators optional) |
| OS | **GS/OS** (System 6.0.x) — Memory Manager, Tool Locator, Finder |
| Net | **Marinetti** TCP/IP (GS/OS tool set **$36**) over **Uthernet II** (W5100) |
| Crypto | Stephen Heumann's **`sheumann/65816-crypto`** SHA-256 (hand-tuned asm) |
| Graphics | **Super Hi-Res** 320 mode, bare-metal writes to `$E12000` |
| Pool | **Stratum v1** (`mining.subscribe/authorize/notify/submit/set_difficulty`) |
| Host tools | **Golden Gate** (ORCA/C `occ`), **AppleCommander**, **Ample/MAME** |

We go *through* the Marinetti API (we are **not** banging W5100 registers directly), and we
get the toolbox "for free" because we run as a GS/OS app rather than a bare ProDOS-8 binary.

---

## 5. Architecture

```
+-------------------------------------------------------------+
|  GS/OS application (S16)  -  miner/viz.c                     |
|                                                             |
|  +-----------------------+      +------------------------+  |
|  |  Mining core          |      |  Stratum client        |  |
|  |  mine.c               |<---->|  stratum.c             |  |
|  |  - 80-byte header     | job  |  - cooperative state   |  |
|  |  - SHA-256d (midstate)|      |    machine (non-block) |  |
|  |  - zbits / submit gate| share|  - Marinetti tcpip.h   |  |
|  +-----------------------+      +-----------+------------+  |
|             |                               | TCPIPPoll()    |
|             v                               v                |
|  +-----------------------+      +------------------------+  |
|  |  SHR dashboard        |      |  Marinetti ($36) ->     |  |
|  |  viz.c draw_* + blits |      |  Uthernet II (W5100)    |  |
|  |  $E12000 / SCB / pal  |      +------------------------+  |
|  +-----------------------+                                  |
+-------------------------------------------------------------+
```

The inner nonce loop **yields to `TCPIPPoll()`** on a fixed budget (every N nonces) so the
cursor, panel, and network never starve each other. Networking is a **cooperative state
machine** that advances one step per call and never blocks — essential under GS/OS, where a
blocking read would freeze the GUI.

Supporting modules: `numfmt.c` (fast integer/hex formatting, no libc bloat), `cfg.c` (CONFIG
load/save to `SYSFILES/MINER.CONF`), `mlog.c` (on-disk rotating diagnostic log).

---

## 6. Deep dive: the SHA-256d core

Bitcoin uses **double SHA-256** over the 80-byte header. On a 16-bit CPU with no barrel
shifter and no 32-bit ALU, that's the hot path.

- **`int` is 16-bit in ORCA/C.** Every SHA-256 word is 32-bit → `unsigned long` everywhere.
  Forget once and you silently truncate.
- We use Stephen Heumann's **`sheumann/65816-crypto`** `sha256.asm` — it assembles 32-bit
  words from 2× 16-bit ops (vs 4× 8-bit on a 6502) and exposes the **64-byte compression
  primitive**, which is what makes the midstate trick feasible.
- **Midstate optimization (M5):** the header is two 64-byte SHA blocks; only the second
  changes as we roll the nonce. So we compute the **midstate** over block 0 **once** per job
  and, per nonce, do **1 compression for block 1** + the second SHA of the double-hash →
  **~2 compressions/nonce** instead of 3. Measured ~5–6 → ~8 H/s in Ample.
- **Validation:** `minecore.c` reproduces the Python oracle **byte-for-byte** — merkle root,
  found nonce `0x1a`, and hash `005b58e4…597cc4` all match `scripts/mock_pool.py`'s self-test.

The marquee bug here (all-zero hashes) is in §12 — it's a great GS gotcha.

---

## 7. Deep dive: networking (Marinetti + Stratum)

Marinetti is a **cooperative/polled** GS/OS tool set: you must call `TCPIPPoll()` regularly
or the stack stalls. The client (`stratum.c`) is therefore a **non-blocking state machine**:

- **Connect** → `TCPIPLogin` / `TCPIPOpenTCP`, poll `TCPIPStatusTCP` until ESTABLISHED with a
  `GetTick`-based timeout and exponential backoff (1 s → cap 5 s).
- **Handshake** → `mining.subscribe`, then `mining.authorize` (username = **`wallet.worker`**;
  solo pools authorize on the BTC address).
- **Job** → parse `mining.notify` with a tiny hand-rolled tokenizer (no JSON lib), honoring
  the advertised `extranonce2_size` and folding the real `merkle_branch` into the merkle root.
- **Submit** → only for hashes that clear `set_difficulty` (see §3).
- **Resilience** → real-pool hardening added **8 KB / 4 KB** RX/TX buffers, **512 B** coinbase
  parts, a corrected failover budget (prefer-primary single-shot re-probe), and
  `CONN-REFUSED / TIMEOUT / DROPPED` detail in the on-disk log.

Marinetti's C interface (`tcpip.h`) turned out to be **self-contained** — no external SDK
header needed; record layouts + constants come from the Marinetti equates. One sharp edge:
**Marinetti IP LongWords are first-octet-in-LOW-byte** (network order in a little-endian
LongWord) — easy to get backwards. See POC §7/§7a and the DHCP/DNS gotcha in §12.

---

## 8. Deep dive: the SHR dashboard

The UI is **bare-metal Super Hi-Res** (320 mode), written directly to `$E12000` (pixels),
`$E19D00` (per-scanline SCBs), `$E19E00` (palettes) — the authentic bare-metal approach, and it
sidesteps QuickDraw startup cost.

- **The chassis is procedural.** `viz/build_frame.py` draws one "metal sheet" with recessed
  wells, 2-tone chiseled bevels, chamfered corners, and engraved grooves — the single source
  of truth for every layout coordinate → `frame_proc.png` → `frame.shr` (the `PANEL` plate).
- **A fixed palette contract** (`viz/spec.py`) gives each scanline band a fixed set of accent
  inks plus a per-zone grey ramp recomputed from the local metal gradient → hundreds of greys
  down the panel from only 16 slots/row. The header band holds black, an 8-step grey ramp,
  blue/cyan/green/red/**gold**/white.
- **`viz.c` draws the dynamic layer** on top with a hard-edged 4×6 font and the same palette
  indices: labels, live values, the rainbow hash scope (driven by real SHA output bytes), a
  rolling 30s hashrate VU, the coin badge, buttons, and TCP lamp.
- The whole method (coords-as-contract, per-band palettes, common pitfalls) is generalized in
  [`IIgs_SHR_UI_PLAYBOOK.md`](IIgs_SHR_UI_PLAYBOOK.md) so future GS apps can reuse it.

---

## 9. The toolchain

We build **natively on a Mac** and produce a **real IIGS binary** that runs in Ample/MAME
*and* on hardware.

- **Golden Gate** (Kelvin Sherlock) runs the 1990s ORCA tools as native macOS commands —
  "Rosetta/Wine for ORCA." `occ` is the ORCA/C driver (compiles **and** links the runtime).
- Build (large memory model, required for the full app):

  ```
  occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c -L. -llib65816hash -o viz
  iix chtyp -t s16 viz
  ```

- **Inject** onto the ProDOS disk image with **AppleCommander** (`scripts/inject_gsminer.sh`): the app
  goes to `/GSMINER/GSMINE<rev>`, data plates + icon to `SYSFILES/` and `Icons/`.
- **Run** in **Ample** (MAME-based GS emulator) with Marinetti + Uthernet II configured, or on
  real iron.

> **Build gotcha (documented once so you don't lose a day):** Golden Gate reads the GS/OS file
> type from the macOS Finder-info xattr. If a sandbox blocks that xattr, every ORCA **library**
> looks like type `00` instead of `$B2` and the linker can't resolve `printf`/startup. Build
> outside the sandbox, or `chtyp` the libs back to `$B2`.

---

## 10. The build, milestone by milestone

| # | Milestone | Result |
|---|---|---|
| **M0** | Toolchain + hello-world `.S16` | ✅ `occ` builds/links; `sizeof(int)==2` confirmed |
| **M1** | SHA-256 core + vectors | ✅ Heumann's `SHA256("abc")=ba7816bf…`; GS == macOS `shasum` |
| **M1.5** | Mining core offline | ✅ `minecore.c` == Python oracle byte-for-byte (nonce `0x1a`) |
| **M2** | Marinetti echo | ✅ `echo.c` round-trips a line vs `echo_pool.py` in Ample |
| **M3** | Stratum handshake | ✅ subscribe/authorize, parse one live `mining.notify` |
| **M4** | Mining loop + shares | ✅ end-to-end → `mining.submit` → **pool ACCEPTED share #1** |
| **M5** | Midstate optimization | ✅ `GSMINE70` — ~2 compressions/nonce; ~5–6 → ~8 H/s |
| **M6** | Real-pool hardening | ✅ `GSMINE71` — `wallet.worker`, `extranonce2`, merkle branch, submit filter |
| **M6.5** | Real-pool LIVE + ship hardening | ✅ `GSMINE72`→`88` — **verified live on `solo.ckpool.org`** |
| **M7** | Visualization / SHR dashboard | 🔄 shipping (Stage 1+2); method in the SHR playbook |
| **M8a/b** | LIVE Stratum wired into dashboard | ✅ cooperative client; panel stays live during net I/O |
| **M-rel** | Ship prep + in-app logo | 🔄 `GSMINE89`→`94` — README, `SYSFILES/`, icon, **gold logo** |
| **M-accel** | Acceleration story | ⬜ measure stock vs accelerated multiplier |

Full prose for every milestone (with dates and code pointers) is in POC §11 + §16.

---

## 11. Revision history

The rev lives in `#define APPVER "V0.N"` in `miner/viz.c`; `scripts/inject_gsminer.sh` injects it as
`GSMINE<NN>` and keeps the previous rev on the disk as a rollback. Key checkpoints are frozen
under [`baseline/`](baseline/):

| Rev | Tag | What landed |
|---|---|---|
| V0.57 | `stage1_GSMINE57` | Stage-1 dashboard (no networking) frozen baseline |
| V0.68 | `stage2_m8a_GSMINE68` | M8a — cooperative connect state machine |
| V0.69 | `stage2_m8b_GSMINE69` | M8b — mock-pool ACCEPT A/B reference |
| V0.70 | `m5_midstate_GSMINE70` | M5 — midstate optimization |
| V0.71 | `m6_realpool_GSMINE71` | M6 — real-pool correctness |
| V0.76 | `m6fix_GSMINE76` | **SHA large-model (`occ -b`) DBR fix** (§12) |
| V0.72–88 | — | ship hardening: buffers, `mlog.c`, failover polish, clean-ship disk |
| V0.89–93 | — | `SYSFILES/`, Finder icon, live JOBS/NET DIFF/BLOCK, README |
| **V0.94** | `GSMINE94` | **in-app gold GS / red MINER logo** + dynamic `v0.94` stamp (§14) |
| **V0.95** | `GSMINE95` | **🏆 first LIVE mining on real hardware** — Marinetti `LoadOneTool(54)`+`TCPIPStartUp()` fix, portable app-relative paths, 2IMG-wrapped CFFA-mountable disk, `GSMINER` volume |

---

## 12. War stories (the bugs that cost us days)

- **All-zero hashes.** The `sha256_context` **must** be `NewHandle`'d in **bank 0**,
  page-aligned, `attrFixed | attrNoCross`. Allocate it anywhere else and the asm writes to the
  wrong bank → every hash comes back all-zero. Copy the library's own test-harness allocation.
- **The 16-bit `int` trap.** 32-bit SHA words in a 16-bit-`int` compiler. `unsigned long` or bust.
- **The SHA large-model DBR bug (`GSMINE76`).** After going to the large memory model (`occ -b`,
  needed once the app grew), `lib65816hash` produced **wrong digests** — real-pool shares started
  rejecting even though the mock path passed. Root cause: a **data bank register (DBR)** assumption
  in the asm that held under the small model but not the large one. Patched in `sha256.asm`; this
  is the bug that gated "verified live on a real pool."
- **DHCP clobbers DNS.** In the emulator, Marinetti's DNS kept getting reset by DHCP to a host that
  wouldn't resolve; manual CDEV settings got clobbered on lease renewal. Use a pool **IP** or a LAN
  resolver path during dev. (POC §7a.)
- **Emulator write-back clobber + the "hidden file" scare.** Ample mounts the apps floppy
  **read-write** and caches it in memory; a host-side inject isn't visible until the disk is fully
  released, and on close the emulator can flush a **stale** copy back over your inject. Separately,
  AppleCommander stamps freshly-injected files with a ProDOS access bit + no GS/OS Finder catalog
  record, so the **GS Finder hides the new rev** until the desktop is rebuilt. Both look like
  "the inject failed" but the on-disk directory is correct (verified by parsing the raw ProDOS
  volume directory). Fix: fully release the disk before injecting; rebuild the desktop / toggle
  "show hidden" once.
- **Perf regression + recovery.** A draw-path change tanked hashrate ~10 → ~1 H/s; caching the
  per-job `diff_from_nbits` / BIP34 height keyed on `job->gen` restored **~9–10 H/s** (V0.92).

---

## 13. Performance & the "turbo GS" lottery

Measured: **~9–10 H/s** on a stock 2.8 MHz GS (`GSMINE94`), and **22 H/s** on a 14 MHz TransWarp GS
(real hardware). Expected tries to land a share is a geometric distribution with mean ≈ 2^N for an
N-bit target:

| Target | ~Hashes | @ ~10 H/s (stock) | @ ~22 H/s (14 MHz TWGS†) | @ ~1 kH/s (turbo GS\*) |
|---|---|---|---|---|
| 32 bits | 4.3×10⁹ | ~14 years | ~6 years | ~50 days |
| 45 bits | 3.5×10¹³ | ~110k years | ~50k years | ~1.1k years |

†**Measured**, not projected — 22 H/s on the real 14 MHz TWGS rig (≈2× stock, so the wait roughly
halves). \*Rough linear clock scaling at a hypothetical ~300 MHz; **not** a measured build. The point:
the submit gate isn't a "no" — it's a filter. A valid hit always submits.

---

## 14. The in-app logo pipeline

The logo well used to draw plain text. V0.94 renders the approved chiseled wordmark — a
**gold (idx 14) `GS` monogram + red (idx 13) `MINER`**, black-outlined with a grey chrome bevel,
plus a **dynamic lowercase `v0.94`** version stamp — at runtime. The trick is a clean
**proof → palette indices → runtime blit** pipeline (reusable for any GS app):

1. `viz/logo_depth_proof.py` `render_well_idx()` rasterises the art to **SHR header-band palette
   indices** (0=black, 1–8 grey, 13=red, 14=gold, 15=white; **255 = transparent**) and emits
   **`miner/logo_gs.h`** — no version baked in.
2. `viz.c` `draw_logo()` blits every non-255 index via `setpix()`, then `text_r()` stamps the
   dynamic `APPVER` on top (so a rev bump updates the version for free).

Palette reality: the header band has flat gold/red but **no dark-gold/dark-red**, so depth comes
from the black outline + grey bevel, not a colour gradient — the on-iron blit is intentionally
flatter than the Mac RGB proof. The logo well was widened **82 → 85 px** in `build_frame.py` so
MINER's `R` clears the border at natural spacing, and `drawchar()` got a baseline-aligned
lowercase `v` glyph (the font is otherwise uppercase-only). `APPVER` stays `"V0.94"` so the inject
rev parser still works. Full detail: POC §16k.

---

## 15. Build it yourself

**Prerequisites:** macOS with **Golden Gate** (ORCA/C), **Java** + **AppleCommander.jar**, and
**Ample** (or a real GS with GS/OS + Marinetti + Uthernet II). The patched SHA library
(`miner/lib65816hash`) and `sha256.h` are vendored.

```sh
# 1. (optional) regenerate the chrome + logo
cd viz
python3 build_frame.py                       # -> frame_proc.png
python3 frame2shr_contract.py frame_proc.png frame.shr
cd .. && python3 viz/logo_depth_proof.py     # -> miner/logo_gs.h

# 2. build the app (large memory model)
cd miner
occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c -L. -llib65816hash -o viz
iix chtyp -t s16 viz

# 3. inject onto the disk image (reads APPVER for the rev)
cd .. && scripts/inject_gsminer.sh

# 4. run GSMINE<rev> in Ample, or build a clean release disk:
scripts/make_master.sh
```

**Test without a pool:** run `scripts/mock_pool.py` on the Mac, point CONFIG at its LAN IP (8-bit easy
target), and watch the full submit → ACCEPT path. Pull logs off the disk with `scripts/dump_minerlog.sh`.

---

## 16. Repo layout

| Path | Role |
|---|---|
| `miner/viz.c` | GS/OS app: dashboard + draw primitives + main loop |
| `miner/mine.c`, `numfmt.c`, `cfg.c`, `mlog.c` | mining core, formatters, CONFIG, on-disk log |
| `miner/stratum.c`, `tcpip.h` | non-blocking Stratum client + Marinetti interface |
| `miner/lib65816hash`, `sha256.h` | vendored Heumann SHA-256 (patched for large model) |
| `miner/logo_gs.h` | generated logo bitmap (palette indices) |
| `viz/build_frame.py`, `spec.py`, `frame2shr_contract.py` | procedural chrome → `frame.shr` |
| `viz/logo_depth_proof.py`, `font4x6.py`, `font5x7.py` | logo proof + emitter, fonts |
| `scripts/mock_pool.py` | Mac-side dev Stratum pool / correctness oracle |
| `scripts/inject_gsminer.sh`, `make_master.sh`, `wrap_2mg.py`, `dump_minerlog.sh` | disk inject / release / 2IMG wrap / log tools |
| `branding/` | logo proofs + final dashboard captures |
| `baseline/` | frozen rev checkpoints (see §11) |
| `IIGS_BITCOIN_MINER_POC.md` | the exhaustive lab notebook |
| `IIgs_SHR_UI_PLAYBOOK.md` | reusable GS SHR-UI method |

---

## 17. Credits & references

- **Charles Mangin** — Apple //e **8BITCOIN** (2019), the direct inspiration.
- **Stephen Heumann** — **`sheumann/65816-crypto`**, the hand-tuned 65816 SHA-256 we build on.
- **Kelvin Sherlock** — **Golden Gate** (native ORCA toolchain) + ORCA tooling.
- **Marinetti** — the GS/OS TCP/IP stack (community-maintained) that makes any of this possible.
- **Antoine Vignau / Brutal Deluxe** — canonical IIGS graphics/tooling references
  (PicViewer, Convert3200, TrueConvert, Merlin 32) leaned on throughout.
- **ckpool / `solo.ckpool.org`** and **public-pool.io** — the solo pools used for live testing.

---

## 18. Roadmap

### The performance reality (the honest cowboy take)

Before V0.95 we did a deliberate "what would the 65816 console cowboys zero in on?" pass over
the whole core. The conclusion is worth stating plainly, because it's counter-intuitive:

**There is almost no free hashrate left in software.** Double-SHA-256 is *fundamentally* two full
block compressions per nonce, and Heumann's `65816-crypto` compression is already a hand-tuned,
direct-page-resident, 8-way-unrolled implementation (per-state guard words for shift-across
rotates, precomputed index constants, schedule computed 16 words at a time). At the measured
~9–10 H/s stock, each hash is ~280k cycles — essentially *all* of it inside those two
compressions. So:

- The obvious C-side cleanups (specialising the second hash instead of routing it through the
  generic `init`/`update`/`finalize`, killing a redundant byte-swap, not re-zeroing the block
  each nonce) are **< 1%** — code hygiene, not speed, and they'd disturb the one path that's
  *proven bit-identical* to the reference by `miner/mstest.c` + `scripts/mock_pool.py`. Not worth the risk.
- A full **nonce-specialised double-SHA in asm** — precompute block-1's midstate *past round 2*
  (the nonce is `W3`, so rounds 0–2 and schedule words `W0–W17` are job-constant), fold `K+W` on
  the ~23 constant-`W` rounds, early-out on the final word — is real cowboy work but lands at
  **~2–5%**, not a multiplier. Tabled for bragging rights, not for ship.
- **Clock is the only real lever.** ~10 H/s stock → ~21 H/s on a 14 MHz TransWarp GS (~2×) → a
  ZipGS / faster accelerator / emulator-turbo scales roughly linearly. This is the genuine story
  (8BITCOIN's "exponential" cowboy win came from rewriting a *naïve* SHA; ours started tuned).

Code-health verdict from the same pass: the hash path is correct and the entire Stratum→header
buffer chain is provably bounded (`next_str` truncates; every temp matches its `g_job` field;
`g_hexcat[1152]` ≥ the 1088-char max; `coinbase[640]` ≥ 544 B). **No change recommended to the
V0.95 core** — it ships as-is.

### Where the payoff actually is

- **`sha256d-65816` mini-library** — package the proven double-SHA core as a clean, documented,
  reusable 65816 unit so the next person doesn't have to rediscover the midstate/endianness dance.

### Explicitly dropped (and why)

- **"Share found" celebration** — the target is unreachable at our hashrate, so it would *never*
  fire. Building a celebration for an event that can't happen is pure dead code.
- **CPU-speed auto-detect / H-per-MHz readout** — the TWGS / ZipGS / AppleSqueezer / emulator-turbo
  permutation matrix makes a reliable reading more trouble than it's worth.
- **Best-bits leaderboard** — the realistic user count is too small for cross-machine bragging.

### Ship items

- **Gold-master release** — `scripts/make_master.sh` clean `.2mg` (CFFA-ready, `GSMINER` volume) on GitHub
  Releases + LICENSE.
- **M-accel** — measured hashrate on accelerated hardware/emulation, replacing the "turbo GS"
  asterisk with a real multiplier.

---

*Got a few quadrillion years to kill? Boot it up.*
