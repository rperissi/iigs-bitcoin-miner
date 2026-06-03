# Apple IIgs Bitcoin Miner — Proof of Concept Design

A deliberately, gloriously **useless** retro project: a real, pool-connected
Bitcoin miner running natively on a 2.8 MHz (or accelerated) Apple IIgs over
Ethernet. It will never find a block. That is the point. The goal is a
*complete, correct* mining stack — SHA-256d + Stratum + a live pool connection —
on a 65816, in the spirit of option8's 8-bit `8BITCOIN` (Apple IIe, 6502) but
pushed onto the 16-bit GS with a hardware network card.

> Status: **beta — FIRST LIVE MINING ON REAL HARDWARE (`GSMINE95`, V0.95, 2026-06-03).**
> Confirmed end-to-end Stratum mining on a real Apple IIgs (14 MHz TWGS, Uthernet II in slot 2,
> `solo.ckpool.org`) — see §16m. Mining core +
> networking + SHR dashboard **proven end-to-end**. Stage 2 COMPLETE (M8a + M8b, §10c):
> full Stratum on Marinetti, pool-job mining, mock shares **ACCEPT**ed. M5 midstate
> (`GSMINE70`), M6 real-pool hardening (`GSMINE71`), M6.5 ship campaign (`GSMINE72`→`88`,
> §16a–16d): SHA large-model fix, buffers, `mlog.c`, failover polish — **verified live on
> `solo.ckpool.org`**. Ship-readiness (`GSMINE89`→`93`, §16f–16g-2): **`SYSFILES/`**
> layout, custom Finder icon, live **JOBS** metric, live **NET DIFF** / **BLOCK** decode,
> perf fix (~**9–10 H/s** on stock 2.8 MHz). **User-facing README** drafted (LIVE vs DEMO,
> submit **filtering** vs hard no-submit, turbo-GS lottery math, mock pool, build-from-source
> prerequisites). **Held before inject:** in-app logo/branding (`branding/`, `viz/build_logo.py`).
> **Next:** gold-master disk → GitHub release (FB post); source drop + baseline cleanup a few
> days after. Optional: M-accel on real TWGS/ZipGS. See §11 / §16h.

---

## 1. The point (and the reality check)

option8's 6502 `8BITCOIN` did ~**3.8 hashes/sec** after Qkumba's optimization
(~269k cycles/SHA-256 down from >1M). On the GS we expect to do meaningfully
better thanks to:

- 16-bit registers (32-bit SHA-256 words assembled from 2× 16-bit ops instead of 4× 8-bit),
- higher clock (2.8 MHz stock; 7–18 MHz on a TransWarp GS / ZipGS),
- Stephen Heumann's hand-optimized `sheumann/65816-crypto` SHA-256.

### Hashrate math (the whole joke)

Bitcoin mining = find a nonce so that `SHA256(SHA256(80-byte header))` is below
the network target. A single attempt is **two** SHA-256 compressions over the
header (which spans 2 blocks) plus the second hash — effectively **3 SHA-256
block compressions per nonce** (midstate optimization, §5, cuts the first
block out of the inner loop, leaving ~2).

Rough envelope:

| Machine | est. hashes/sec | seconds per block (at current difficulty) |
|---|---|---|
| Apple IIe (6502, 1 MHz) — measured | ~3.8 H/s | ~256 trillion years |
| IIgs (65816, 2.8 MHz) — **measured** (`GSMINE94`) | ~**9–10 H/s** | quadrillions of years |
| IIgs (65816, 2.8 MHz) — earlier Ample soak | ~8 H/s | (pre–V0.92 draw-path fix) |
| IIgs + TransWarp (~8–18 MHz) — projected | ~60–250 H/s | trillions of years |

So: from "heat death of several universes" to merely "heat death of one." We
declare a hash with **2+ leading zero bytes** an "interesting find" (option8's
threshold), purely so the screen has something to celebrate.

### Deliverables (what we'll have to show the community)

1. **The miner app** — a GS/OS S16 that really mines (real SHA-256d + real
   Stratum + a live pool connection). Same binary runs in **Ample for dev and
   on real Apple IIgs hardware** for the demo.
2. **Accelerator story** — measured hashrate stock (2.8 MHz) vs accelerated
   (TransWarp/ZipGS); "it goes faster with acceleration" is a headline result.
3. **`mock_pool.py`** — the Mac-side test pool/oracle (already built); a tidy
   show-and-tell in its own right, and what makes the build reproducible.
4. **The visualization** — a 1990s demoscene / SETI@Home-waveform-style live
   readout of the hashing (added after the bones work; see §10a).
5. **Write-up** — the networking spike, the hashrate/acceleration reality, and
   the gloriously useless "years-to-a-block" math.

---

## 2. High-level architecture

```
+---------------------------------------------------------------+
|  IIgs (GS/OS app, S16)                                        |
|                                                               |
|  +-----------+   share/work   +---------------------------+   |
|  | Mining UI |<-------------->|  Miner core               |   |
|  | (text/SHR)|                |  - block-header assembly  |   |
|  +-----------+                |  - nonce loop             |   |
|        ^                      |  - SHA-256d (midstate)    |   |
|        |                      +-------------+-------------+   |
|        |                                    | hash result      |
|        |                                    v                  |
|  +-----+------------------------------------+-------------+    |
|  | Stratum client (line-based JSON over one TCP socket)   |    |
|  +-----------------------------+--------------------------+    |
|                                | Marinetti TCP calls           |
|                                v (TCP set, tool $36)           |
|  +---------------------------------------------------------+   |
|  | Marinetti TCP/IP  ->  Uthernet II link layer (W5100)    |   |
|  +---------------------------------------------------------+   |
+--------------------------------|------------------------------+
                                 | Ethernet (vmnet in Ample / real LAN on iron)
                                 v
                  +-----------------------------+
                  |  Stratum pool               |
                  |  - real: solo.ckpool / pool |
                  |  - dev: mock pool on Mac     |
                  +-----------------------------+
```

Four cleanly separable modules, each independently testable:

1. **SHA-256d core** — pure computation, testable against known vectors offline.
2. **Stratum client** — line protocol, testable against the mock pool (§8).
3. **Miner core** — header assembly + nonce loop, glues 1 and 2.
4. **UI** — status/hashrate; least important, can be plain text first.

---

## 3. Why this MUST be a GS/OS app (not ProDOS 8)

The 3200-color gradient work was bare-metal ProDOS 8. **The miner cannot be.**
Marinetti (the GS TCP/IP stack) is a **GS/OS tool set ($36)** and requires the
GS/OS environment, the Tool Locator, Memory Manager, etc. So:

- Target: **GS/OS S16 application** (`$B3`), launched from the Finder.
- We get Memory Manager, the Tool Locator, and Marinetti "for free."
- Marinetti must already be installed + connected (it is, on the Starter Kit
  image we've been using — TCP/IP CDEV + Uthernet II link layer).

This also means we lean on the **Speccie IIgs Starter Kit** image as our base
volume (it ships Marinetti + Uthernet II LL already configured).

---

## 4. Toolchain decision — LOCKED ✅ (ORCA/C + ORCA/M via Golden Gate)

**Decision made and installed (2026-05-29):** Option A. ORCA/C 2.2.1 + ORCA/M
(`Asm65816`) + the OMF Linker run natively on macOS via **Golden Gate** (`iix`
2.1.0, root `/Library/GoldenGate`). Compilers/libraries were extracted from
**Opus ][: The Software** (`Byteworks.po`) with `opus-extractor`. Verified by
building and running a hello-world `.S16` (M0) and Heumann's SHA-256 self-test
(M1). We edit in Cursor, build with `occ`, and out pops a real IIgs binary.

> **Build gotcha (host sandbox):** Golden Gate reads each file's GS/OS *type*
> from a macOS Finder-info xattr in `/Library/GoldenGate`. A restricted sandbox
> that blocks reading those xattrs makes every library look like type `00`, so
> the linker silently skips `ORCALib`/`SysLib` → unresolved `printf`/startup.
> Run builds **unsandboxed** (or in a normal Terminal); then `ORCALib` reports
> its true type `b2` and links fine. (`.h` files still resolve because their
> type is guessed from the extension, which masks the issue at first.)

### Option A — ORCA/C + ORCA/M via Golden Gate  ✅ CHOSEN

**What is Golden Gate?** "Rosetta/Wine for 1990s IIgs command-line tools." A
65816 emulator + a native reimplementation of *just* the GS/OS + toolbox parts
needed to run **text-based** IIgs command-line programs (no screen/sound/keyboard).
It lets the old ORCA compilers (`orcac`, `orcam`, the linker) run as if they were
native Mac commands, so we edit in Cursor and build natively — and out pops a real
IIgs OMF/S16 binary that runs **in Ample *and* on real hardware**. Golden Gate is
build-only; it can't *run* a networked GUI app (that's Ample/iron's job).

- `sheumann/65816-crypto` is written in **ORCA/M assembly** with an ORCA/C
  callable interface — drops in natively (OMF library linking).
- Marinetti is a GS/OS tool set; callable from ORCA/C/ORCA/M — write the
  Stratum/JSON glue in **C** instead of assembly (big time saver).
- General-purpose win beyond this project: ORCA/C for toolbox/GS/OS apps,
  ORCA/M to build canonical Brutal Deluxe/Vignau source, OMF-library linking.

**Cost / what to buy (one-time):**
- **Golden Gate** — $10 (Juiced.GS). Compatibility layer only; *no compilers*.
- **Opus ][: The Software** (Juiced.GS) — contains **ORCA/M** + ORCA/C 2.1 base
  (+ Pascal, Modula-2, linker, MakeLib, DumpObj, 35 manuals, GSoft BASIC).
  ORCA/M is **not** freely available anywhere else — this is the gating buy.
- Free on GitHub: ORCA/C **2.2 updates**, the OMF **Linker**, **ORCALib**
  (but they need the Opus ][ base install to be useful).

### Option B — cc65 (ca65/ld65), our existing setup
- Reuse the build/disk/AppleCommander flow we already have.
- Cost: must **port** SHA-256 to ca65 syntax, and **hand-write Marinetti glue**
  (tool dispatch to $E10000 with the $36 tool set), matching the GS/OS ABI.
  More work, more bug surface, on the two hardest modules.

**Recommendation (now adopted):** build the miner in ORCA via Golden Gate (A).
It minimized risk on the crypto + networking modules, which are exactly the
parts we don't want to reinvent. Keep cc65 for any bare-metal graphics flourishes.

> Decision **locked**: Option A, installed and proven (M0/M1/M1.5). cc65 remains
> available for any standalone ProDOS-8 graphics experiments only.

---

## 5. SHA-256d core

Bitcoin block header is **80 bytes**:

| Field | Bytes | Notes |
|---|---|---|
| version | 4 | from pool `notify` |
| prev block hash | 32 | from pool |
| merkle root | 32 | built from coinbase + branches (§6) |
| ntime | 4 | from pool, may roll |
| nbits | 4 | target in compact form |
| **nonce** | 4 | **what we iterate** |

`blockhash = SHA256(SHA256(header))`. SHA-256 processes 64-byte blocks, so an
80-byte header = **2 blocks** (block0 = bytes 0..63, block1 = bytes 64..79 +
padding). The second SHA-256 hashes the 32-byte first digest = **1 block**.

### Midstate optimization (the key perf lever)

Bytes 0..63 (version, prev hash, first 28 bytes of merkle root) **do not change
as we roll the nonce**. So compute the SHA-256 **midstate** over block0 *once*
per work item, then the inner nonce loop only does:

- 1× SHA-256 compression for block1 (contains the nonce), starting from midstate,
- 1× SHA-256 compression for the second hash.

≈ **2 compressions per nonce** instead of 3. This is exactly the trick Qkumba
used on the 6502 version. `sheumann/65816-crypto` exposes `sha256_processblock`
(the per-64-byte compression primitive), so the midstate wrapper is feasible.

> **Not yet applied.** `minecore.c` (§5a) currently calls the full
> `sha256_init/update/finalize` path per nonce (the straightforward ~3
> compressions). Midstate is the headline **optimization milestone** once the
> end-to-end miner works — expected ~1.5× on the inner loop.

### Testing the core offline
Validate against canonical vectors before it ever touches the network:
- `SHA256("abc")` = `ba7816bf...` ✅ (Heumann self-test under Golden Gate)
- macOS `shasum -a 256` of a real file == GS `sha256sum` of the same file ✅
- The mock pool's exact job → exact share (nonce/merkle/hash) ✅ (§5a)

---

## 5a. Mining core — VERIFIED OFFLINE ✅ (2026-05-29)

`miner/minecore.c` reproduces, in ORCA/C on the 65816, the **exact** computation
`mock_pool.py` does in Python — with no network and no emulator GUI, just Golden
Gate. It hardcodes the mock pool's demo job, builds the 80-byte header per the
spec, SHA-256d's it across a nonce sweep, and reports the first share.

Result is **identical to the Python oracle, byte-for-byte**:

```
merkle_root        = 52165af81eb097916fd11696b69de50620d515b8a470e665b2cb291a88ae83fb
FOUND nonce        = 0x0000001a (26)  zero_bytes=1
hash (big-endian)  = 005b58e460fc7a91b6d25ba0333d99d3cab99741d5e4470916dcb63285597cc4
```

This proves SHA-256d + header assembly + **endianness** + nonce search are all
correct on the GS toolchain — i.e. the entire compute half of the miner. What's
left is to feed it a *live* job over the wire (§6/§7) instead of a hardcoded one.

### Gotchas discovered (bank for the write-up)
- **`int` is 16-bit in ORCA/C.** SHA-256 words are 32-bit → use `unsigned long`
  (`sizeof(long)==4`), never `int` (`sizeof(int)==2`), for header fields, nonce,
  and lengths.
- **The `sha256_context` MUST be `NewHandle`'d in bank 0**, `attrFixed |
  attrPage | attrBank | attrNoCross`, at location `0x000000` — the asm uses
  direct-page/bank-0 addressing. A plain `static` global silently produces
  **all-zero hashes** (the asm writes to the wrong bank). Copy the allocation
  pattern from the library's own `sha256test.c`.
- Golden Gate runs 65816 code at *native Mac* speed, so timings here are **not**
  representative — real hashrate gets measured in Ample (§ accel / M-accel).

---

## 6. Stratum protocol (pool side)

Stratum v1 is **line-delimited JSON over one long-lived TCP socket** — which is
why today's FTP *control channel* success already proves the transport we need.

Minimal client flow:

1. `mining.subscribe` → server returns subscription + **extranonce1** + extranonce2_size.
2. `mining.authorize` (worker, password) → `true`.
3. Server pushes `mining.set_difficulty` and `mining.notify` (job): version,
   prevhash, coinbase1/coinbase2, merkle_branch[], nbits, ntime, clean_jobs.
4. **Build coinbase** = coinbase1 + extranonce1 + extranonce2 + coinbase2;
   `coinbase_hash = SHA256d(coinbase)`.
5. **Merkle root** = fold `coinbase_hash` through `merkle_branch[]` with SHA256d.
6. Assemble header, roll nonce (and extranonce2/ntime when exhausted), hash.
7. On a hash under (our toy) target → `mining.submit`(worker, job_id,
   extranonce2, ntime, nonce).

### JSON: keep it dumb
We do **not** need a general JSON parser. Messages are predictable; a
hand-rolled tokenizer that pulls fields by key (`"method"`, `"params"`, fixed
positions in the params array) is enough and far smaller/faster. Line framing =
read until `\n`.

### Endianness footguns
Bitcoin mixes little- and big-endian all over (prevhash is byte-reversed in
32-bit words, nbits/ntime/nonce are LE in the header but presented as hex,
etc.). Budget real time for an endianness test rig — this is where most miners
break, not the SHA itself.

---

## 7. Marinetti TCP call sequence (tool set $36)

The Stratum client sits on these Marinetti calls (names per the Marinetti
Programmer's Reference; all via the Tool Locator):

```
TCPIPStatus            ; confirm Marinetti loaded + connected (link up)
TCPIPGetMyIPAddress    ; sanity: we have a DHCP lease
; (optional) TCPIPDNRNameToIP   ; resolve pool hostname -> IP (skip if using IP)
ipid = TCPIPLogin(userID, destIP, destPort, TOS, TTL)
TCPIPOpenTCP(ipid)                       ; SYN; poll until ESTABLISHED
loop:
  TCPIPPoll()                            ; service the stack
  TCPIPWriteTCP(ipid, buf, len, push=1, urgent=0)   ; send JSON line
  TCPIPReadTCP(ipid, ...)                ; pull bytes; reassemble lines
  TCPIPStatusTCP(ipid)                   ; detect remote close / errors
TCPIPCloseTCP(ipid)
TCPIPLogout(ipid)
```

Notes:
- Marinetti is **cooperative/polled**; we must call `TCPIPPoll` regularly. The
  mining inner loop should periodically yield to `TCPIPPoll` (e.g. every N
  nonces or on a VBL tick) so the socket doesn't stall.
- Use the **destIP** directly (skip DNS) for the dev/mock pool; add
  `TCPIPDNRNameToIP` later for real pools by hostname.
- The W5100 (Uthernet II) gives Marinetti a hardware assist, but we still go
  through the Marinetti API — we are **not** banging W5100 registers directly.

---

## 7a. Marinetti from ORCA/C — VERIFIED ✅ (2026-05-29)

`miner/echo.c` + `miner/tcpip.h`: a GS/OS S16 that dials OUT to the Mac
(`192.168.2.1:2000`, `echo_pool.py`), sends a line, reads the echo, and reports
the verdict back over the socket. Verified end-to-end in Ample: GS connects,
`HELLO FROM MAC` read, `PING FROM GS` echoed, `GS-ECHO-OK` returned. This is the
code-level version of the §9 manual proof — the whole transport the miner needs.

**No external Marinetti SDK header was needed.** `tcpip.h` is self-contained,
built from the canonical ORCA assembly definitions that ship with Golden Gate:
- dispatch numbers + param order from `QASystem/Macros/TCPIP.MACS.S`
- record layouts (`rrBuff`, `srBuff`) + constants from the Marinetti equates
- ORCA/C calls tools via `inline(0xCCTT, dispatcher)` (CC=call, TT=$36 toolset);
  the function's return type sizes the result space (matches the macro PHA/PHS).

### Gotchas discovered (these recur in M3 endianness work)
- **Marinetti IP LongWords are first-octet-in-LOW-byte** (network order in
  memory): `192.168.2.1` = `0x0102A8C0`, NOT `0xC0A80201`. Confirmed when
  `GetMyIPAddress` returned `0x0702A8C0` for 192.168.2.7. Use a
  `MAKE_IP(a,b,c,d)=a|b<<8|c<<16|d<<24` macro; print low byte first.
- **`TCPIPLogin` TTL must be non-zero** — TTL 0 = the SYN is dropped before it
  leaves the stack ("not established" with no error). Use `0x40` (64), as
  marignotti does.
- **`Ref` is `Long`** in ORCA/C `types.h`, so pass record pointers as `(Ref)&rec`
  (explicit cast) or you get "type conflict".
- Build is S16-shell type (`$B5`) by default; `iix chtyp -t s16` → `$B3` so the
  **Finder can launch it**. Package onto a *copy* of the boot image with
  AppleCommander: `cat echo | java -jar AppleCommander.jar -p DEV.2mg ECHO S16 '$0000'`.
- Marinetti must be **connected** (DHCP) before the app runs; the dev pool
  (`echo_pool.py` / `mock_pool.py`) must be **listening** or TCP can't establish.

---

## 8. Test harness — mock Stratum pool on the Mac

We can develop the entire stack against Ample without ever touching a real
pool. Reuse exactly the pattern from today's networking spike (`echo_pool.py` /
`ftp_test.py`), but speak Stratum:

`mock_pool.py` — **BUILT** ✅ (this repo). Self-tests on startup by brute-forcing
a valid nonce in Python (currently solvable in ~26 tries), and contains the
authoritative 80-byte header layout spec in its docstring. Responsibilities:
- Listen on `192.168.2.1:3333` (vmnet gateway; GS dials out — the proven path).
- Answer `mining.subscribe` / `mining.authorize`.
- Push a **fixed, known** `mining.notify` job with an **absurdly easy target**
  (e.g. difficulty so low that a few thousand nonces finds a "share"), so the
  GS visibly submits shares within seconds — instant feedback loop.
- Log/ACK `mining.submit`, optionally **verify** the submitted nonce in Python
  (recompute SHA256d) to confirm the GS's hashing is *correct*, not just
  plausible. This doubles as the cross-check oracle for the SHA core.

This gives a tight dev loop: change GS code → AppleCommander into the image →
launch in Ample → watch shares hit the Mac console. Later, point the same
client at a real low-diff pool (e.g. a solo CKPool / a public testnet pool) by
changing host/port.

> The mock pool is the single most valuable piece of tooling for this project —
> it makes an "untestable on emulator" idea fully testable. (Tracked as a todo.)

---

## 8a. Emulator DNS for hostname tests — host-side daemon (REVERSIBLE) ⚠️

To exercise the **DNS resolution** path (the `RESOLVE` badge + the config-page name
check, §10c) the emulated GS must get a *working* resolver from DHCP. It doesn't by
default, and the fix lives entirely on the **Mac host** — so it's documented here as
a clearly-reversible piece of test infrastructure, **not** part of the GS app.

**Why it's needed (what we found 2026-05-31):**
- Ample networks the Uthernet II (`-sl3 uthernet2`) via Apple's **vmnet "shared"
  (NAT)** mode. macOS `bootpd` is the DHCP server on `bridge100` (`192.168.2.1`).
- vmnet hands the guest **DNS = the `.1` gateway**, whose forwarder doesn't reliably
  answer the guest → hostnames won't resolve. Marinetti's DNS field shows `192.168.2.1`.
- **Neither MAME nor Ample expose a DNS setting**, and vmnet **regenerates
  `/etc/bootpd.plist` on every network start**, so a manual edit never survives an
  Ample restart. (Manually setting DNS in the Marinetti CDEV works but DHCP clobbers
  it on each reconnect — painful across iterations.)

**The fix — a watch-and-repin LaunchDaemon (`tools/dnspush/`):**
- `ample-dns-push.sh` rewrites the DHCP `dns-server` option to `8.8.8.8`/`8.8.4.4`
  and HUPs `bootpd`; `com.local.ample-dns-push.plist` runs it via `WatchPaths` on
  `/etc/bootpd.plist`, so it re-pins automatically whenever vmnet regenerates the file.
  8.8.8.8 is reachable straight through the vmnet NAT (no local DNS server needed).
- Install / verify / uninstall: see **`tools/dnspush/README.md`**.

**Impact / safety (verified):** touches **only** the DHCP option offered to
vmnet/Internet-Sharing guests on `192.168.2.x`. It does **not** alter the Mac's own
resolver, routes, `pf`, or any interface, and is **orthogonal to Tailscale** (which
manages the Mac's resolver / `100.100.100.100` / exit-node routes — a different
layer). Only side effect: *any* shared-network guest gets `8.8.8.8` for DNS.

> **UNWIRE (do this to fully remove the daemon):**
> ```sh
> sudo launchctl unload -w /Library/LaunchDaemons/com.local.ample-dns-push.plist
> sudo rm /Library/LaunchDaemons/com.local.ample-dns-push.plist /usr/local/sbin/ample-dns-push.sh
> ```
> On its next start vmnet regenerates `bootpd.plist` with the default `.1` → **zero
> residue**. (Alternative to the daemon entirely: switch Ample to **bridged**
> networking so the GS leases from your real router, or just set DNS by hand in the
> Marinetti CDEV.)

---

## 9. Networking validation — DONE ✅ (2026-05-29)

Proven in **Ample** (MAME-based, vmnet) before writing any miner code:

- Emulated machine: `apple2gs -sl3 uthernet2 -sl7 cffa2` booting the Speccie
  Starter Kit image; ROM 3; 4 MB.
- **DHCP lease obtained** by the GS (Marinetti + Uthernet II LL, slot 3) →
  bidirectional traffic confirmed (Discover/Offer/Request/Ack round trip).
- **Outbound TCP + multi-turn dialog confirmed**: SAFE2 opened a TCP control
  channel to a Mac FTP server (`192.168.2.1:21`) and completed the
  `USER/PASS` login handshake. This is structurally identical to a Stratum
  socket → **the transport the miner needs works.**

Known limitation (does **not** affect the miner):
- **Active-mode FTP fails** through Ample's vmnet: vmnet is NAT-like and blocks
  *inbound* connections to the guest, so the FTP **data** channel (server →
  GS) times out. PASV would be required, but the emulated W5100 wouldn't open
  the second socket in our tests. **Irrelevant to mining** — Stratum is a single
  GS-initiated socket (the direction that works).

Practical takeaways baked into the design:
- The miner only ever **dials out** (GS → pool). ✅ supported.
- Dev pool binds to `192.168.2.1` (vmnet gateway), GS reaches it directly.
- Critical gotcha discovered: **Marinetti's link-layer slot setting is separate
  from the GS Control Panel "Your Card" slot** — both must point at slot 3, or
  the card never transmits (silent failure, nothing on `tcpdump`).

---

## 9a. Full miner — VERIFIED END-TO-END ✅ (2026-05-29)

`miner/miner.c` (+ `tcpip.h`, `sha256.h`, `lib65816hash`) is the whole stack in
one S16: Marinetti TCP (M2 transport) + Stratum v1 + the verified mining core
(M1.5). Run in Ample against `mock_pool.py`, the GS:

1. `TCPIPLogin/OpenTCP` → established to `192.168.2.1:3333`.
2. `mining.subscribe` + `mining.authorize`; parses `extranonce1` + one
   `mining.notify` (hand-rolled JSON; demo job has an empty merkle_branch so the
   quoted params are exactly job_id/prevhash/coinb1/coinb2/version/nbits/ntime).
3. Assembles coinbase = `coinb1 + en1 + en2("00000000") + coinb2`, merkle =
   `SHA256d(coinbase)`, builds the 80-byte header from the **live** job.
4. Nonce search at the pool's easy target (1 leading zero byte), yielding to
   `TCPIPPoll` every 64 nonces.
5. On a hit → `mining.submit [worker, job_id, en2, ntime, nonce(hex)]`.

**Result:** nonce `0x0000001a` → hash `005b58e4…597cc4`, pool re-verified in
Python and logged `SHARE #1 ACCEPTED` — byte-identical to the pool's own
self-test. The header/endianness/merkle math is correct on real 65816 code.

### Gotchas discovered (deploy/host side)
- **Homebrew `openjdk` is keg-only**: it is NOT symlinked into the system Java
  location, so Apple's `/usr/bin/java` stub reports "Unable to locate a Java
  Runtime" even though Java is installed. AppleCommander injection must call the
  full path `"/opt/homebrew/opt/openjdk/bin/java"`. Baked into `inject_miner.sh`.
- **GS/OS Finder desktop cache hides a delete+recreate**: re-injecting under the
  *same* filename (delete old `MINER`, write new `MINER`) can leave the Finder
  showing the stale entry → the app appears "missing" on the desktop even though
  the catalog/bytes are correct (verified by extracting it back: identical SHA).
  Fix: inject under a **fresh name** (e.g. `GSMINER`) or rebuild the desktop.
- **MAME write-back vs read-only**: with the image mounted read-write, MAME can
  flush its in-RAM disk over host-side injections on exit/reset. `inject_miner.sh`
  refuses to run while `mame64` is live; **fully Cmd-Q Ample before injecting**.
  (Confirmed harmless when mtime is unchanged after a run = MAME didn't write.)
- **One-command deploy**: `./inject_miner.sh GSMINER` (correct Java path, refuses
  if the emulator is running, deletes+adds+verifies the S16 at the volume root).

---

## 9b. Acceleration / hashrate — MEASURED ✅ (2026-05-29)

`miner/bench.c` (`BENCH` on the disk): hashes an 80-byte header repeatedly for a
fixed ~5-second window using the clock-independent 60 Hz tick (`GetTick`), then
prints `H/s`. Because it counts hashes per *emulated* second, the figure is
immune to the emulator's wall-clock throttle (verified: Ample at 200%/500% boots
GS/OS visibly faster but `BENCH` stays put — the throttle scales CPU and tick
together).

**Measured in Ample (one SHA-256d = the mining unit = ~3 block compressions):**

| GS Control Panel | Raw count | H/s |
|---|---|---|
| **Fast** | 512 hashes / 340 ticks | **90 H/s** |
| Normal ("Slow") | 64 hashes / 673 ticks | 5.7 H/s |

**Determining the real clock.** Fast:Slow measured = **15.9:1**, but a real IIgs
is **2.8:1** (2.8 MHz vs 1.0 MHz) — so a MAME speed mode is inauthentic. The SHA
cycle budget settles which: 90 H/s ⇒ ~**10,300 cycles per SHA-256 block**, the
expected figure for this hand-optimized 65816 code, i.e. an authentic **2.8 MHz**.
For Fast to be 16 MHz, the code would need ~59,000 cycles/block (~6× slower than
even a naive build) — impossible. So **Fast = authentic 2.8 MHz = 90 H/s** (stock
baseline); MAME's *Slow* mode is the unfaithful one (running ~0.18 MHz, not 1 MHz)
and is discarded as a reference.

**Acceleration is linear in clock** (SHA is pure CPU, no I/O), so from the 2.8 MHz
anchor:

| Clock | Real-world equivalent | Projected H/s |
|---|---|---|
| 1.0 MHz | original Apple II | ~32 |
| **2.8 MHz** | **stock Apple IIgs** | **90 (measured)** |
| 7–8 MHz | TransWarp GS | ~225–257 |
| 12.5 MHz | Zip GS (typical) | ~402 |
| 16 MHz | hot Zip GS | ~514 |

Caveat / TODO: the accelerated rows are *projections* from the measured stock
point. Ample exposes only a wall-clock throttle (not an emulated-CPU overclock we
could reach), so the accelerated numbers will be **empirically confirmed on real
accelerated hardware** (TransWarp GS / ZipGS) during the on-iron run. The linear
model is safe for SHA, but real accelerators add wait-state/cache nuances worth
measuring.

For perspective, the gag for the writeup: at 90 H/s a stock GS is ~**10^18×**
slower than one modern ASIC, so expected time to find a real block is on the order
of 10^13 years. Gloriously useless — exactly the point.

---

## 10. Build & deploy workflow

```
[Mac] edit source  ->  build (Golden Gate/ORCA  or  cc65)  ->  S16 binary
      -> AppleCommander: add MINER.S16 to a copy of the Starter Kit .2mg
      -> Ample: boot image (sl7 cffa2), Marinetti already connected (sl3 uthernet2)
      -> double-click MINER from Finder
      -> mock_pool.py running on Mac (192.168.2.1:3333) prints shares
```

- Keep a **dedicated working copy** of the Starter Kit image (don't pollute the
  pristine one) with the miner added.
- AppleCommander flow already documented in `APPLECOMMANDER_QUICKSTART.md` /
  `HOW_TO_ADD_FILES_TO_DISK.md`.
- On **real iron** later: same S16, copied to a real ProDOS/GSOS volume, real
  Uthernet II in a slot, real pool. The accelerated GS is where the (still
  hopeless but less hopeless) hashrate lives.

---

## 10a. Visualization — the demoscene / SETI@Home vibe

The "wow" layer for the community demo: early-90s **demoscene** energy +
**SETI@Home** live-activity readout — a brushed-metal mining console with LED
readouts, a rainbow hash spectrograph, VU meters, and the "133 Trillion Yrs"
joke. Target screen is the locked concept mockup
(`assets/gsminer1-*.png`).

> **Full reusable methodology lives in `IIgs_SHR_UI_PLAYBOOK.md`.** Read it before
> touching SHR UI. Summary of the architecture and the key decisions below.

**Architecture — static frame plate + code-drawn content.** A static SHR
"chassis" plate is loaded at runtime; everything dynamic (labels, values,
spectrograph, VU bars, lamps, button fills, coin) is **drawn by C on top** into
recessed wells. This buys sharpness, exact palette control, fast iteration, and
cheap animation. (Baking text/graphics into a downscaled image muddies them.)

**Frame is procedural, native 320×200 — LOCKED ✅ (2026-05-30).** After proving
the AI-image→downscale route pixelates and the AI won't reliably leave empty
label strips, the chassis is rendered procedurally in `viz/build_frame.py`
(→ `viz/frame_proc.png`). Benefits: pixel-exact layout, smooth metal, and the
builder *is* the source of truth for every coordinate the C code reuses.

**Smooth metal = clean contiguous greys, not dithering.** The "silk" metal look
comes from smooth gradients snapped to a single **neutral, face-weighted grey
ramp** (no bayer/error-diffusion on flat metal — that reads as 8-bit noise).
Dimension from subtle 2-tone bevels + chamfered corners; "one sheet" with screws
in the corners (+ wallet ends).

**Palette contract** (`viz/spec.py`): 16 colors/scanline, band palettes
(header/wallet/mid/bottom) selected per row via SCB, a shared grey ramp in low
indices (kills speckle), and fixed accent indices the C code draws into
(`H_CYAN`, `M_LED`, `B_AMBER`, rainbow 8, red/gold). Converter
`frame2shr_contract.py` quantizes the frame against this fixed contract →
`frame.shr`.

**Text** = hard-edged **4×6** chassis font (`viz/font4x6.py`) for labels/values, plus
a **5×7 case-sensitive wallet font** (`viz/font5x7w.py`) for the mixed-case wallet
string; C glyph tables generated from the same sources (`gen_font_c.py` →
`miner/font_gs.h` + `miner/font_w_gs.h`) so Mac proof == GS.

**Proven on hardware ✅** — `miner/viz.c` loads the plate and code-draws the full
populated panel (sample values) in Ample: razor-sharp labels, rainbow scope, VU
bars, all from the contract.

Pipeline files: `build_frame.py` (chassis), `spec.py` (contract),
`frame2shr_contract.py` (→ SHR), `font5x7.py`/`gen_font_c.py` (font),
`render_panel.py` (Mac full-panel proof), `miner/viz.c` (GS loader + draw).

**Live data wired ✅ (M7b).** `miner/viz.c` now runs a **real local SHA-256d
mining loop** behind the dashboard and feeds every readout from it: hashrate
(smoothed EMA), shares, nonce, best-bits, uptime, hashes, the rainbow scope from
real digest bytes, and the 30s VU/graph from rolling hashrate. RUN/STOP/CONFIG are
mouse-clickable (self-drawn cursor, `ReadMouse` polling). This is **Stage 1** — see
§10b for the demo/live split and the config page.

Constraints: runs under **GS/OS** (Marinetti), so once LIVE networking is wired
(§10c) SHR drawing must yield to `TCPIPPoll` and not starve the nonce loop — keep
viz on a frame budget (per VBL / per N hashes), not per-hash. The current Stage-1
demo loop already self-gates drawing to the pointer/frame, not per-hash.

---

## 10b. Stage-1 demo app + config page + DEMO/LIVE mode — SHIPPING ✅ (2026-05-31)

The dashboard ships as a complete, polished operator app **before** the live
network is wired, so it runs on any GS (or Ample) with **no Marinetti and no Mac
pool**. Two modes:

- **DEMO (default)** — the local SHA-256d loop (§10a M7b) drives the whole panel.
  Coin badge reads **`DEMO MODE`** (gold). No pool, no payout — pure visualization
  of real hashing. This is what we demo when there's no network.
- **LIVE** — *(Stage 2, §10c)* will connect Marinetti → mock/real Stratum pool and
  drive the panel from pool work instead of the local job.

**Mode + operator settings live on a CONFIG page** (`config.shr` plate +
`cfg_screen()` modal in `viz.c`). It's a second chassis built by the same
procedural pipeline (`viz/build_config.py` → `config_frame.png` →
`viz/config2shr.py` → `config.shr`; Mac proof `viz/render_config.py`).

Config page contents (all coords mirrored between `build_config.py`, `viz.c`, and
`contract_cfg.h`):
- **Editable fields** (live RAM text entry — TAB cycles, type to append, DELETE
  erases, click-to-focus): WORKER, WALLET (5×7 mixed-case font), POOL + PORT,
  BACKUP + PORT. 13px-tall LCD wells.
- **DEMO/LIVE toggle** = a **LIVE | DEMO button pair** (blue / amber, the active
  one drawn pressed), mirroring the main panel's RUN | STOP pair. Lives in the
  right-hand button stack: **SAVE → DEFAULTS → LIVE·DEMO → CANCEL·QUIT**, vertically
  aligned with the help panel (y≈101–189).
- **Help panel** (flat black well): DEMO/LIVE explainer + key hints.
- Title bar: `DEMO MODE - SET TCP/POOL/WALLET FOR LIVE` (demo) / `GS MINER  -
  CONFIGURATION` (live).

**Persistence:** `MINER.CONF` (one field per line, MODE first), loaded on boot,
written on SAVE. Legacy 6-line files (no MODE row) load with `MODE=DEMO` prepended.
DEFAULTS button restores compiled defaults (ship wallet
`3CfSNGtkdpGMyKWx57Vr93MdHyP2UQgKao`).

### Network state model + error states (the part that matters for LIVE)

Marinetti is probed with **`TCPIPGetConnectStatus()` + `toolerror()`**, giving three
states (`cf_net_state()` in `viz.c`):

| Probe result | State | Meaning |
|---|---|---|
| `toolerror() != 0` | `NET_NONE` | Marinetti tool set ($36) **not loaded** |
| ok, returns false | `NET_NO_LINK` | stack up, **no IP link** (not connected) |
| ok, returns true | `NET_UP` | Marinetti up **and** IP link connected |

Behavior matrix (production intent):

| State | Save/Toggle LIVE? | Coin badge | TCP lamp | Mining (RUN) |
|---|---|---|---|---|
| DEMO | always allowed | `DEMO MODE` (gold) | grey | auto-starts (local SHA) |
| LIVE, no Marinetti | **blocked** → `LIVE: MARINETTI NOT FOUND` | `NO TCP` (red) | stopped; RUN no-op |
| LIVE, no link | **blocked** → `LIVE: TCP/IP NOT CONNECTED` | `NO IP` (red) | stopped; RUN no-op |
| LIVE, connected | allowed | `SHA-256D` (cyan) | green | auto-starts on boot/save |

- Error surfacing: **config title bar** (red) on a blocked LIVE click/save; **coin
  badge** (`NO TCP`/`NO IP`, red) + **TCP lamp** (grey/red/green) on the main panel.
- If the link **drops while LIVE mining**, the loop auto-stops, badge → `NO IP`, and
  the lamp flips — re-checked each main-loop pass (`tcp_refresh()`), only redrawn on
  state change.
- DEMO never shows a network fault (TCP lamp stays grey — it's a network-only
  indicator, not a "demo" indicator).

> Note: in Stage 1, `NET_UP` means **Marinetti reports an IP link**, not "connected
> to a pool." Pool-socket health is a Stage-2 layer on top (§10c).

### Build / inject (dashboard app)

```
# both chassis + the binary, then inject as the next GSMINE## rev
cd viz && python3 build_frame.py  && python3 frame2shr_contract.py frame_proc.png frame.shr
         python3 build_config.py && python3 render_config.py && python3 config2shr.py
cd ../miner && occ -O255 -w255 viz.c stratum.c -L. -llib65816hash -o viz && iix chtyp -t s16 viz
cd .. && ./inject_gsminer.sh -f
```

- `inject_gsminer.sh` reads `#define APPVER "V0.NN"` from `viz.c` and injects as
  **`GSMINE<NN>`** (S16), refreshing `PANEL` (`frame.shr`) + `CONFIG` (`config.shr`).
  Keeps the **current + previous** rev on the floppy, purges older ones. `-f` allows
  injecting while Ample runs (eject/re-insert the floppy to see it).
- The fresh-name-per-rev scheme dodges the GS/OS Finder desktop cache (§9a gotcha).
- To reset persisted settings, delete `MINER.CONF` from the image so ship defaults
  apply.

### UI gotchas banked (full method in the SHR playbook §12)

- **Config load truncation**: truncating each line to `cf_max[n]` *while reading*
  clipped WORKER to 4 chars / WALLET to 16. Fix: don't truncate on read; truncate
  only when copying into the field buffer with that field's own max.
- **Badge color trap**: `B_AMBER` (index 11) is **cyan** in the header band — use
  `H_GOLD` (14) for the gold `DEMO MODE` badge. (Band-relative ink, playbook §3.)
- **Runtime buttons cover baked screws**: the chassis bakes corner screws, but
  button draws painted over the bottom-right one. Fix: **redraw screws after the
  buttons** each time (`cfg_draw_screws()` last in `cfg_draw_static()`).
- **Geometry lives in three places** — `build_config.py`, `render_config.py`, and
  `viz.c`/`contract_cfg.h` must agree on every coordinate. Change one, change all.

---

> **Stage-1 baseline LOCKED ✅ (2026-05-31): `GSMINE57` (V0.57).** The final
> non-Marinetti/Stratum rev is frozen at `baseline/stage1_GSMINE57/` (full
> buildable source + the exact `viz` S16 + `frame.shr`/`config.shr`). Stage 2 starts
> on a **fresh rev** (V0.58+). See that folder's `README.md`.
>
> **Stage-2 M8a checkpoint LOCKED ✅ (2026-05-31): `GSMINE68` (V0.68).** The verified
> connect + resilience + DNS layer (no Stratum protocol yet) is frozen at
> `baseline/stage2_m8a_GSMINE68/` (adds `stratum.c`/`.h` + the DNR `tcpip.h`).
>
> **Stage-2 M8b checkpoint LOCKED ✅ (2026-05-31): `GSMINE69` (V0.69).** The **full
> Stratum protocol** (subscribe/authorize/notify/submit, pool-job mining, accepted
> shares, `POOL ERR` fault) is frozen at `baseline/stage2_m8b_GSMINE69/`. This completes
> Stage 2. All three checkpoints are **pinned** in `inject_gsminer.sh`
> (`PINS="GSMINE57 GSMINE68 GSMINE69"`) so the rolling purge never removes them. Next
> work (M5 midstate / M6 real pool / M-accel) starts on fresh revs (V0.70+).

---

## 10c. Stage 2 — wire LIVE Marinetti/Stratum into the dashboard ✅ (M8a + M8b)

The compute + transport + UI all exist independently; Stage 2 is **fusing the
proven `miner.c` networking (§9a) into the `viz.c` dashboard** so LIVE mode mines
against the Mac mock pool with the readouts live.

Plan:
1. **Lift the Stratum/Marinetti client out of `miner.c`** into a module the
   dashboard can call (login/open → subscribe/authorize → parse `notify` → build
   header → submit), keyed off the CONFIG **POOL/PORT/WORKER/WALLET** fields instead
   of hardcoded constants.
2. **LIVE mode swaps the work source**: when `NET_UP` and mode=LIVE, the nonce loop
   runs the *pool's* job (not the local demo job); on a hit → `mining.submit`.
3. **Yield to `TCPIPPoll`** on the existing frame budget (every N nonces / per VBL)
   so the socket and the SHR draw coexist — never poll per-hash.
4. **Drive the TCP lamp + badge from real socket state** (extend `cf_net_state()` /
   `tcp_refresh()` to reflect ESTABLISHED / activity, not just link-up), and add
   **TX/RX flash** (`TCP_ACT`) on traffic.
5. **Test against `mock_pool.py`** on `192.168.2.1:3333` in Ample: confirm a share
   is accepted **and** the dashboard shows it (shares counter ticks, lamp flashes).
6. Then point at a real low-diff pool (DNS via `TCPIPDNRNameToIP`) for the meme
   screenshot (M6).

### Progress — M8a connect layer + DNS ✅ (2026-05-31, `GSMINE58`→`GSMINE68`)

Steps 1, 4, and the DNS half of 6 are **built and on disk**; the protocol layers
(subscribe/authorize/notify/submit) and the LIVE work-source swap (step 2) landed in
M8b below (`GSMINE69`).

- **`stratum.c` / `stratum.h` — shared non-blocking client (M8a: connect only).**
  Lifted the Marinetti flow out of the blocking `miner.c` into a **cooperative state
  machine**: `strat_poll()` advances one step per main-loop pass and services
  `TCPIPPoll()` without ever blocking, so the cursor + SHR panel stay live while the
  socket comes up. `viz.c` LIVE mode calls `strat_config/start/poll/stop`.
- **Resilience for "touchy" pools/hardware:** per-pool retry budget
  (`POOL_MAX_RETRY 8`) with an escalating 1–5s backoff; **eager failover to the
  configured backup pool** after `FAILOVER_AFTER 3` consecutive misses (keeps
  alternating so whichever recovers first wins); fast-fail on refused (SYN→CLOSED);
  link faults (no Marinetti / no IP link) re-probe on a timer **without** burning the
  pool budget. Settles honestly to a terminal fault only once the whole episode is spent.
- **DNS hostname resolution (`TCPIPDNRNameToIP`, inline `$2136`).** Pools may be
  dotted-quads *or* hostnames. Connect-time: a name kicks off an async DNR
  (`STR_RESOLVING`, bounded ~10s) before the socket opens; literal IPs skip it. Plus a
  **config-page SAVE-time validation** that resolves the pool (and backup) name and
  blocks the save with a clear note if it can't. Interface was sourced verbatim from
  Golden Gate's `QASystem/Equates/E16.TCPIP` + `Macros/TCPIP.MACS.S` (DNR record:
  `DNRstatus@0`, `DNRIPaddress@2`; status `DNR_Pending/OK/Failed/NoDNSEntry/Cancelled`;
  name passed as a **Pascal string**) — added to `tcpip.h`. Host-side DNS for the
  emulator: see **§8a** (reversible daemon).
- **UI state surfacing.** Main-page badge + config-well text are driven off the live
  `STR_*` state:

  | `STR_*` | Main badge | Config well |
  |---|---|---|
  | `RESOLVING` | `RESOLVE` (gold) | `RESOLVING POOL NAME ...` |
  | `OPENING` | `CONNECT` (gold) | `CONNECTING TO POOL ...` |
  | `CONNECTED` | `SHA-256D` (cyan) | `LIVE MODE - CONNECTED TO POOL` / `... BACKUP POOL` |
  | `RETRY` | `RETRY` (red) | `CONNECTING TO POOL ...` |
  | `NO_IP` | `NO LINK` (red) | `TCP/IP NOT CONNECTED - FIX OR USE DEMO` |
  | `NO_TCP` | `NO MARINETTI` (red) | `MARINETTI NOT LOADED - USE DEMO MODE` |
  | `CONN_FAIL` | `POOL FAIL` (red) | `POOL UNREACHABLE - CHECK ADDRESS OR USE DEMO` |
  | `DNS_FAIL` | `DNS FAIL` (red) | `CANNOT RESOLVE POOL NAME - CHECK DNS OR USE IP` |

  `START` is enabled only in `CONNECTED`. A non-resolving name still fails over (the
  backup may be reachable) but, if the budget runs out with DNS as the last cause, the
  terminal state is `DNS_FAIL` so the reason is honest.

**Tested in Ample (V0.58–V0.66):** connect with/without Marinetti & link; killing the
mock pool mid-session → red lamp → `RETRY`/`CONNECT` cycle → reconnect picks back up;
budget exhaustion → `POOL FAIL`. **DNS paths VERIFIED ✅ (V0.67/68, 2026-05-31):** with
the §8a daemon pushing `8.8.8.8` via DHCP, a real hostname (`pool2.ex.com`) shows
`RESOLVING` then saves/connects; an invalid name (`pool2.ex.comm`) fails the config
SAVE-time check (`POOL: CANNOT RESOLVE NAME`) and blocks the save (form stays open).

### Progress — M8b Stratum protocol ✅ (2026-05-31, `GSMINE69`, V0.69)

The last functional gap is closed: on `CONNECTED` the client now speaks the protocol,
mines the pool's own job, and submits accepted shares — verified in Ample.

- **Handshake + states.** `stratum.c` adds `STR_SUBSCRIBED` / `STR_MINING` /
  `STR_POOL_FAIL`. On `CONNECTED` it sends `mining.subscribe` + `mining.authorize`
  (worker from CONFIG) → `STR_SUBSCRIBED`; the first `mining.notify` → `STR_MINING`.
  A reconnect re-enters the handshake from scratch.
- **Non-blocking line JSON.** `pump_socket()` + `read_line()` accumulate one line at a
  time without blocking; `process_line()` dispatches by shape: subscribe reply (extracts
  the `en1` extranonce), `mining.notify` (parses the job into a `StratJob`, bumps
  `g_job.gen`), and submit verdicts. `msg_id()` parses the JSON `id` tolerant of
  whitespace, so the `authorize` reply (`"result":true`) is never miscounted as a share.
- **LIVE work-source swap.** `viz.c` rebuilds the 80-byte header from `strat_job()`
  whenever `gen` changes, mines that job, and calls `strat_submit(nonce)` on a hit; the
  nonce loop still yields to `TCPIPPoll` on the frame budget. DEMO mode is untouched
  (local job). `START`/run is keyed off `STR_MINING`.
- **Verdicts + protocol fault.** `strat_accepted()` / `strat_rejected()` feed the shares
  readout (`accepted / rejected`). A connected-but-misbehaving pool (no job within the
  timeout / auth reject / garbage) → `STR_POOL_FAIL` → badge `POOL ERR`, well `POOL ERROR
  - CHECK WORKER/WALLET OR USE DEMO`, with the same retry/backoff + backup failover.
- **Lamp + UI.** TX/RX activity pulses the TCP lamp light-green/dark-green (`H_GREEN_DK`,
  palette idx 9). Badge map gains `SHA-256D` (mining) / `SUBSCRIBE` / `POOL ERR`.

**State → UI additions (on top of the M8a table):**

| `STR_*` | Main badge | Config well |
|---|---|---|
| `SUBSCRIBED` | `SUBSCRIBE` (gold) | `SUBSCRIBING TO POOL ...` |
| `MINING` | `SHA-256D` (cyan) | `LIVE MODE - MINING ON [BACKUP] POOL` |
| `POOL_FAIL` | `POOL ERR` (red) | `POOL ERROR - CHECK WORKER/WALLET OR USE DEMO` |

**Tested in Ample (V0.69):** subscribe/authorize → `sent job 'demo1'` → repeated
`mining.submit` → `SHARE #N ACCEPTED` (nonce `0x1a`, hash `005b58e4…597cc4`); on-screen
shares ticked to `8 / 0`, lamp pulsed, graph + readouts live. Same binary is now either a
self-running DEMO (no network) or a real pool miner (LIVE), selectable from CONFIG.

**UI polish folded into this rev:** Block-ETA gag moved to the wide ticker, spelled-out
and adaptive (`BLOCK ETA 2.4 QUADRILLION YRS`), freeing the small slot for `BEST`;
`ODDS`/`BEST` labels centered + aligned (clear of the screw); tactile press-flash on
CONFIG/SAVE/DEFAULTS/CANCEL/QUIT; coin-badge status text centered in its groove.

**Locked:** `baseline/stage2_m8b_GSMINE69/` (buildable source + the exact `viz` S16 +
plates); `GSMINE69` pinned in `inject_gsminer.sh`.

## 11. Milestones (live board)

- **M0 — Toolchain up. ✅** Golden Gate/ORCA builds & runs a real `.S16`
  (`miner/hello.cc` → prints, confirms `sizeof(int)==2`).
- **M1 — SHA-256 core + vectors. ✅** Heumann's `sha256test` gives
  `SHA256("abc")=ba7816bf...`; GS `sha256sum` matches macOS `shasum -a 256`.
- **M1.5 — Mining core offline. ✅** `minecore.c`: SHA-256d + 80-byte header +
  nonce search == Python oracle byte-for-byte (nonce `0x1a`; §5a).
- **M2 — Marinetti echo. ✅** `echo.c`/`tcpip.h`: GS dials out, round-trips a
  line vs `echo_pool.py` in Ample (§7a). Self-contained Marinetti C interface.
- **M3 — Stratum handshake. ✅** `miner/miner.c`: GS subscribes/authorizes,
  parses one live `mining.notify`, assembles the header from the *live* job
  (§9a). Verified in Ample vs `mock_pool.py`.
- **M4 — Mining loop + shares. ✅** Full chain end-to-end in Ample (2026-05-29):
  live job → 80-byte header → SHA-256d nonce search → `mining.submit` →
  **pool re-verified & ACCEPTED share #1**, nonce `0x1a`, hash `005b58e4…597cc4`
  (byte-identical to the pool's self-test). On-screen "find" + key-pause works.
- **M5 — Midstate optimization. ✅ (2026-05-31, `GSMINE70`, V0.70).** Caches the first
  SHA-256 block so the inner loop is ~2 compressions/nonce (down from 3). Verified
  byte-identical offline (`miner/mstest.c`: 0 mismatches/2000, share at `0x1a`) and on
  hardware (~5–6 → ~8 H/s in Ample). Locked at `baseline/m5_midstate_GSMINE70/`. See §15.
- **M6 — Real-pool hardening. ✅ (2026-05-31, `GSMINE71`, V0.71).** Made the Stratum
  client correct against real solo pools (not just the mock): username = `wallet.worker`
  (pools authorize on the BTC address), honors the advertised `extranonce2_size`, folds the
  real `merkle_branch` into the merkle root, and **filters** `mining.submit` on
  `set_difficulty` (leading-zero-bit threshold — hygiene, not a hard "no public submits";
  see §16i). At ~8–10 H/s a real pool's share target is unreachable in practice, so LIVE
  connects + hashes but SHARES stay `0 / 0`; the mock/LAN path uses an easy 8-bit target.
  Built **large memory model** (`occ -b`); mining core + formatters split into
  `mine.c` / `numfmt.c`. Locked at `baseline/m6_realpool_GSMINE71/`. Recommended pools:
  **Solo CKPool** (`solo.ckpool.org:3333`) and **public-pool.io**. *Documented in README:*
  submit filtering vs reject spam; optional "best-effort weak shares" still rejected by pools.
- **M6.5 — Real-pool LIVE proven + ship hardening. ✅ (2026-06-01, `GSMINE72`→`88`, §16).**
  Root-caused the post-M6 share rejects to a **SHA large-model (`occ -b`) DBR bug** and patched
  `sha256.asm` (`GSMINE76`); hardened the Stratum client for real pools (8 KB/4 KB buffers,
  512 B coinbase parts, corrected failover budget — `GSMINE78`); added an on-disk **diagnostic
  logger** (`mlog.c`, two capped 8 KB rotating files + 15 s STAT heartbeat — `GSMINE79`→`81`);
  and shipped polish: **active-pool green highlight**, no poll flicker, **prefer-primary
  single-shot re-probe**, `CONN-REFUSED/TIMEOUT/DROPPED` log detail, **real-pool uppercase ship
  defaults** (`SOLO.CKPOOL.ORG`/`PUBLIC-POOL.IO`), `HS_SECS`→12, clean-ship disk workflow
  (`GSMINE82`→`88`). **Verified mining live on `solo.ckpool.org` from a fresh-boot shipped disk.**
- **M-release — Ship prep + GitHub release.** 🔄 **`GSMINE89`→`94` (V0.89→V0.94), §16f–16g-2 + §16i + §16k.**
  ✅ `SYSFILES/` layout, Finder icon, live JOBS / NET DIFF / BLOCK, perf (~9–10 H/s), user README
  (submit filtering, turbo-GS lottery table, mock pool, build prerequisites), `RELEASE_COMMS.md`,
  **in-app gold GS / red MINER logo** (runtime SHR blit + dynamic version stamp — `GSMINE94`, §16k).
  ⬜ Gold-master `.2mg` on GitHub; FB post; LICENSE; source drop + baseline cleanup. See §16h.
- **M-accel — Acceleration story.** ⬜ Measure real GS hashrate in Ample
  (stock 2.8 MHz vs accelerated); document the multiplier.
- **M7 — Visualization / polish.** 🔄 Stage 1 shipping (§10a/§10b; full method in
  `IIgs_SHR_UI_PLAYBOOK.md`).
  - **M7a — Frame plate LOCKED ✅ (2026-05-30).** Procedural 320×200 chassis
    (`viz/build_frame.py`), palette contract (`viz/spec.py`), clean-grey metal,
    `frame.shr` via `frame2shr_contract.py`; code-draw pipeline proven on
    hardware (`miner/viz.c`) with sample values.
  - **M7b — Live data wired ✅ (2026-05-31).** `viz.c` runs a real local SHA-256d
    loop and feeds every readout (hashrate/shares/nonce/best/uptime/hashes, scope
    from digest bytes, VU/graph from rolling rate). Mouse-clickable RUN/STOP/CONFIG
    with a self-drawn cursor.
  - **M7c — Config page + DEMO/LIVE ✅ (2026-05-31).** Second chassis
    (`build_config.py`/`config2shr.py`); modal editor with live text entry,
    `MINER.CONF` persistence, LIVE|DEMO button toggle, and the Marinetti network
    state model + error states (`NO TCP`/`NO IP` badge, TCP lamp, blocked LIVE save).
    Currently shipping as **`GSMINE57`**.
- **M8 — LIVE networking in the dashboard.** ✅ **DONE** (§10c, `GSMINE69`). Fused the
  proven `miner.c` Marinetti/Stratum client into `viz.c`, keyed off the CONFIG fields, so
  LIVE mode mines the Mac mock pool with the readouts live; yields to `TCPIPPoll`
  on the frame budget; drives TCP lamp/badge from real socket state.
  - **M8a — Connect layer + DNS ✅ (2026-05-31, `GSMINE58`→`GSMINE68`).** Shared
    non-blocking `stratum.c` connect state machine (login/open → ESTABLISHED) with
    retry/backoff, **eager backup-pool failover**, fast-fail, and **DNS hostname
    resolution** (connect-time `STR_RESOLVING` + config SAVE-time validation; DNR
    interface from the Golden Gate `E16.TCPIP` equates). Badges/well + `DNS FAIL`
    fault wired. Host-side emulator DNS via the reversible daemon (§8a).
  - **M8b — Stratum protocol on the live socket. ✅ (2026-05-31, `GSMINE69`, V0.69).**
    subscribe/authorize, parse `mining.notify`, swap the nonce loop to the pool job,
    `mining.submit` with accepted/rejected verdicts, `POOL ERR` protocol-fault state, and
    TX/RX lamp pulse. Verified in Ample vs `mock_pool.py` (`8 / 0` shares accepted).
    Locked at `baseline/stage2_m8b_GSMINE69/`.

---

## 12. Risks & open questions

- **ABI / glue (if cc65):** calling Marinetti ($36) and ORCA-built crypto from
  ca65 is the main risk in Option B. Mitigated by choosing ORCA (Option A).
- **Endianness bugs** in header/merkle assembly — biggest *correctness* risk;
  mitigated by the mock pool re-verifying every submitted share.
- **Marinetti polling discipline** — long SHA loops must yield to `TCPIPPoll`
  or the socket stalls/closes. Design the loop around periodic polling.
- **`sheumann/65816-crypto` interface fit** — RESOLVED: it exposes
  `sha256_processblock` (the per-block compression), so the midstate wrapper is
  feasible (M5). Whole-message path (`init/update/finalize`) already verified.
- **Emulator fidelity** — Ample's W5100 emulation proven for outbound TCP;
  watch for edge cases under sustained traffic. Real iron is the final word.
- **Float of "done"** — this never finds a block. "Done" = correct shares
  accepted by a (mock or real) pool. Define success there, not at a block.

---

## 13. References

- option8 `8BITCOIN` (Apple IIe / 6502 miner; KansasFest 2019) — the inspiration
  and the hashrate-joke benchmark.
- `sheumann/65816-crypto` — hand-optimized 65816 SHA-256 (+MD4/5, SHA-1) for the
  IIgs; the crypto foundation.
- **Marinetti** Programmer's Reference + the docs on the Starter Kit image:
  `/DOCUMENTATION/MARINETTI.DOCS/` (TCP/IP tool set $36 API).
- Stratum v1 protocol (mining.subscribe/authorize/notify/submit).
- Ken Shirriff's manual/vintage-hardware mining series (spiritual predecessor).
- Local toolchain notes: `IIGS_65816_SETUP.md`, `APPLECOMMANDER_QUICKSTART.md`.
- Networking spike artifacts (this repo): `echo_pool.py`, `ftp_test.py`.
- Mock pool + oracle: `mock_pool.py`. Toolchain: `/Library/GoldenGate` (`iix`).
- GS source (this repo): `miner/hello.cc` (M0), `miner/minecore.c` (M1.5);
  crypto lib clone: `65816-crypto/` (built `lib65816hash`, `sha256test`).

### Build cheatsheet (reproduce M0/M1/M1.5)
```
# hello-world S16
cd miner && occ hello.cc -o hello && iix ./hello

# SHA library + self-test
cd 65816-crypto && make sha256test sha256sum && iix ./sha256test

# offline mining core (must print nonce 0x1a)
cd miner && occ -O255 -w255 minecore.c -L. -llib65816hash -o minecore && iix ./minecore
```
(Run in a normal Terminal / unsandboxed so Golden Gate can read the lib file
types — see §4 gotcha.)

---

## 14. Immediate next steps

1. ✅ ~~Lock toolchain + M0 hello S16~~ — DONE (Option A, Golden Gate/ORCA).
2. ✅ ~~`mock_pool.py` easy-target Stratum server + verifier~~ — DONE.
3. ✅ ~~Pull `sheumann/65816-crypto`, confirm compression primitive~~ — DONE
   (`sha256_processblock`; SHA + mining core verified, §5a).
4. ✅ ~~M2 Marinetti echo client~~ — DONE (§7a, `echo.c`).
5. ✅ ~~M3/M4 Stratum handshake + mining loop + accepted shares~~ — DONE (§9a,
   `miner.c` vs `mock_pool.py`).
6. ✅ ~~M7 SHR dashboard (live local demo + config page + DEMO/LIVE)~~ — DONE
   (§10a/§10b, `viz.c`, shipping as `GSMINE57`).
7. ✅ ~~**M8 — wire LIVE networking into the dashboard**~~ — DONE (§10c, `GSMINE69`):
   lifted the Marinetti/Stratum client out of `miner.c` into `stratum.c`/`.h` that
   `viz.c` calls, keyed off the CONFIG POOL/PORT/WORKER/WALLET fields; LIVE mode mines
   `mock_pool.py` with live readouts; `TCPIPPoll` on the frame budget; TCP lamp/badge
   from real socket state.
   - ✅ **M8a** — `stratum.c` non-blocking **connect** machine (retry/backoff,
     backup-pool failover, DNS resolution + config validation, fault badges/well);
     shipping as `GSMINE68`. Host-side emulator DNS = reversible daemon (§8a,
     `tools/dnspush/`).
   - ✅ **M8b** — full Stratum protocol on the live socket (subscribe/authorize/notify/
     submit) + pool-job mining + accepted/rejected verdicts + `POOL ERR` fault + TX/RX
     lamp pulse; `GSMINE69`, verified in Ample (`8 / 0` shares accepted).
8. ✅ ~~**M5** midstate optimization~~ — DONE (`GSMINE70`, V0.70, §15): first-block caching
   → ~2 compressions/nonce, ~5–6 → ~8 H/s in Ample, verified byte-identical (`mstest.c`).
9. ✅ ~~**M6** real-pool hardening~~ — DONE (`GSMINE71`, V0.71): `wallet.worker` auth,
   `extranonce2_size`, `merkle_branch` folding, `set_difficulty` submit gate; large memory
   model (`occ -b`); `mine.c`/`numfmt.c` split. Connects cleanly to Solo CKPool / public-pool;
   at ~8 H/s the worker shows online but submits nothing (no reject spam). Mock keeps flowing
   easy shares via a sub-1 dev difficulty. Locked at `baseline/m6_realpool_GSMINE71/`.
10. 🔄 **Release + optional milestones**: GitHub disk drop + comms (§16h); **M-accel**
    stock-vs-accelerated hashrate on real iron; optional draw-path perf tuning; optional
    outer-hash early-exit (§15). *Declined for ship:* "submit best-effort weak shares" —
    pools reject below target; documented as hygiene filtering in README §16i.

---

## 15. M5 — midstate: prior art + the 65816 angle

### What it is (and why it's not novel)
A Bitcoin header is **80 bytes**, and SHA-256 digests in **64-byte blocks**, so the
header splits into **block 1** (bytes 0–63: version + prevhash + first 28 B of the merkle
root — *constant for a job*) and **block 2** (bytes 64–79: tail of merkle + ntime + nbits
+ **nonce** + padding). Mining is `SHA-256d` = `SHA256(SHA256(header))`. Naïvely that's
**3 compressions/nonce** (block1 + block2 for the inner hash, + 1 for the outer hash over
the 32-byte digest). Since block 1 never changes while you sweep the nonce, you compress
it **once per job**, cache the resulting 8-word state ("the **midstate**"), and per nonce
do only block 2 + the outer hash → **2 compressions/nonce** (~1.5×).

This is **standard and old**, not novel: the original `getwork` JSON-RPC (≈2010–2011)
literally shipped a `midstate` field to clients so they wouldn't recompute block 1. Every
serious CPU/GPU/FPGA miner used it; ASICs hardwire it. We document it as "the standard
first-block caching every real miner uses."

### Prior art on 8-bit: option8/8BITCOIN already did it
Charles Mangin's **8BITCOIN** (Apple IIe / 6502, KansasFest 2019 — the project we cite as
inspiration/benchmark) implements exactly this. From `HASH.s`:
- `CACHEDHASH DS 32 ; storage for the first chunk of first pass. Won't change between
  nonce changes.` — i.e. the midstate.
- "cache then hash" logic (`HASHCACHED` flag, `CHECKCACHE`/`HASHTOCACHE`).
- In his own cycle log it was the **single biggest optimization**:
  `unrolled COPYCHUNK1 → 714,904` then `caching the result of chunk0 on pass0 → 419,137`
  cycles/hash — right in line with the theoretical 3→2.

So our M5 reproduces a 6502-era trick in C on the 65816, using `sheumann/65816-crypto`'s
exposed `sha256_processblock` primitive (`hash[]`/`block[]` are the live DP state/schedule;
state words are little-endian mid-computation and only byte-swapped to big-endian by
`finalize`, so we snapshot/restore `hash[]` raw and swap the inner digest ourselves).

### The 65816 angle — what the GS *can* do that the IIe couldn't
We are **not** optimizing to mine seriously (ETA stays in the quintillions of years);
we're matching the real technique for authenticity, and so the **acceleration story**
(M-accel: TWGS/Applesqueezer) measures the *real* inner loop, not a naïve one. That said,
the GS has legitimate architectural edges over the 6502 worth calling out:
- **16-bit ALU (the big one).** SHA-256 is 32-bit math; the 6502 must do it 8 bits at a
  time (4 ops per 32-bit add), the 65816 in 16-bit mode does 2 ops. `sheumann/65816-crypto`
  is already a hand-tuned 16-bit-wide SHA, so even at the *same* clock a GS needs roughly
  half the instructions option8's IIe did. This is the honest "only the GS" win, and we
  already inherit it.
- **Relocatable Direct Page.** `sha256_processblock` literally points D at the context
  struct so the 64-word schedule + 8 state words are all zero-page-fast. A 6502 has only
  the single fixed `$00` zero page; the 65816 can park multiple DP "register frames" and
  switch with one `TCD`.
- **Block moves (`MVN`/`MVP`).** Hardware copy for the per-nonce block-2 / digest shuffles
  vs a 6502 byte loop.
- *(Speculative, probably not worth it)* two-nonce interleave sharing one midstate via DP
  bank-switching to hide setup overhead — marginal; note it, don't build it.

### Next-tier (after M5, CPU-agnostic, optional)
- **Outer-hash early-exit:** the share test only needs the most-significant word of the
  final hash; bail out of the last compression once it can't have enough leading zero bits.
- **Second-hash first-block caching:** the outer hash's first 64 bytes (digest + fixed
  pad) are partly constant; a smaller, fiddlier win. Not needed for our purposes.

### Plan / status — DONE ✅ (`GSMINE70`, V0.70, 2026-05-31)
1. ✅ **Offline probe** (Golden Gate `iix`, no UI/network): `miner/mstest.c` computes the
   demo header both ways → `equivalence: PASS (0 mismatches / 2000)`,
   `share: ref nonce=0x1a  mid nonce=0x1a  PASS`, `ALL OK`. Proved the primitive first.
2. ✅ Ported `sha256d_header()` + cached `g_midstate` into `viz.c`'s `mine_slice`
   (`midstate_update()` runs at the end of `build_job` / `build_job_live`, so the cache is
   refreshed only on a new DEMO or LIVE job). Verified on hardware: ~5–6 → **~8 H/s** in
   Ample (when the self-drawn cursor isn't being redrawn — it costs frame-loop cycles),
   LIVE shares accepted byte-identically. Locked at `baseline/m5_midstate_GSMINE70/`.

---

## 16. M6+ real-pool campaign, diagnostics & ship polish (`GSMINE72`→`88`)

After M6 (`GSMINE71`) connected cleanly to real pools but (by design) submitted nothing at
real difficulty, three things remained before this could ship to the community: (1) the
**mock pool started rejecting shares**, which had to be root-caused; (2) real pools needed
**connectivity hardening** the mock never exercised; and (3) a round of **diagnostics + UX
polish + ship defaults**. This section is the running log of that campaign.

### 16a. The share-reject saga → a SHA library large-model bug ✅ (`GSMINE72`→`77`)

**Symptom.** After the M6 split to the large memory model (`occ -b`), the mock pool — which
had happily accepted `8 / 0` shares at `GSMINE69` — began **rejecting** submitted shares
(`zero_bytes=0`), non-deterministically (e.g. nonces `0x5c`, `0x248`, `0x33b`, `0x136`).

**False leads (and what we learned from each):**
- **Midstate fast-path suspicion (`GSMINE73`).** Forced LIVE to use full `sha256d` to rule
  out a midstate false-positive. Rejects continued → midstate was *not* the cause. (Reverted.)
- **Stack overflow (`GSMINE74`).** Found large stack-allocated parse buffers in
  `process_line()` / `build_job_live()` on the 65816 and moved them to `static` storage.
  A **real bug worth fixing** (and kept), but rejects continued → not the root cause either.
- **Verify-before-submit gate (`GSMINE75`).** Added a "re-hash and check before submitting"
  band-aid. It masked, not fixed, the problem — **removed in `GSMINE77`** once the real cause
  was found.

**Root cause (`GSMINE76`).** Under the large memory model, `lib65816hash` produced **wrong
hashes**. `65816-crypto/sha256.asm` accessed its round-constant table `k` via DP-indirect
addressing that depends on the **Data Bank Register**; with code/data relocated across banks
by `occ -b`, the DBR was wrong, so constants were read from the wrong bank → silently
corrupted digests. **Fix:** relocate the `k` block into the `SHA256_PROCESSBLOCK` segment and
bracket the routine with `phb`/`phk`/`plb` (set DBR to the code bank on entry, restore before
`rtl`). Reproduced deterministically offline (`xmodtest.c`) and verified the patched lib hashes
byte-identically; mock shares **ACCEPTED** again (`GSMINE77`). This was the single most
important correctness fix of the whole project under the new toolchain.

> Lesson banked for the write-up: **any hand-asm library that reads constants via DP/DBR must
> be audited when you flip to `occ -b`.** The small-model build hid the bug for the entire
> Stage-1/2 run.

### 16b. Real-pool connectivity hardening ✅ (`GSMINE78`)

Real pools (`public-pool.io`, `solo.ckpool.org`) exposed two issues the mock never did:
- **Buffer overflow on real messages.** Real `mining.notify` lines run 1.5–2.5 KB and real
  SegWit coinbases far exceed the mock's. Bumped: socket accumulator `acc` 2 KB→**8 KB**,
  `g_line` 1 KB→**4 KB**, temp parse buffers `c1`/`c2` 256→**512 B**, `StratJob.coinb1/coinb2`
  256→**512 B**, and in `mine.c` `g_hexcat` 640→**1152 B** / `coinbase` 384→**640 B**. Without
  these, fields truncated and `g_job.gen` never advanced (stuck in CONNECT/SUBSCRIBE/RETRY).
- **Retry-budget reset bug.** `g_try`/`g_poolfails` were cleared the instant TCP established,
  so a pool that **connected but failed the handshake** never accrued toward failover →
  infinite loop. Moved the reset to the `STR_SUBSCRIBED`→`STR_MINING` transition (first job in
  hand = a genuinely working session), so a stalled handshake now correctly fails over.

Offline stress harness `miner/brtest.c` (a `ckpool`-shaped job: 12 merkle branches, 8-byte
extranonce2) proved `build_job_live`/`mine_slice` handle real jobs correctly — ruling mining
math out as a freeze cause.

### 16c. On-disk diagnostic logger ✅ (`GSMINE79`→`81`)

A community-friendly, on-disk diagnostic trail (people love log files when chasing connection
quirks), sized for an 800 K floppy.

- **`miner/mlog.c` + `mlog.h`.** Two **8 KB capped, rotating** files: `MINER.LOG` (current) +
  `MINER.OLD` (previous). `mlog_event()` timestamps each line `T+ssss.hh` via `GetTick()`,
  appends (`fopen("a")`), and rotates (`remove`/`rename`) at the cap. ~16 KB total = a couple
  hundred lines of look-back. Validated stdio primitives offline first (`miner/logtest.c`).
- **What it logs (event-driven, not per-hash):** app version + MODE, CFG pool/backup/worker,
  `CONNECT`/`DNS?`/`DNS=`/`TCP-UP`/`HS`/`SUBOK`/`DIFF`/`JOB`/`MINING`, `FAIL`/`FAILOVER`/`TERM`,
  `ACCEPT`/`REJECT`/`SUBMIT`, plus a **`STAT` heartbeat every 15 s** (`STAT_TICKS 900`) with
  hashrate/best/accepted/rejected/pool/uptime. A one-time **`NET`** line dumps IP + link status.
- **Startup-hang fixes (`GSMINE79`→`81`).** Logging at boot first **wedged the machine** (blue
  screen): a burst of Marinetti getters fired while an async DNR was in flight. Fixes:
  (1) reorder so `mlog_open()` + version/MODE/CFG events run **before** `cfg_load_file()` kicks
  off any network; (2) **defer `mlog_netinfo()`** until `STR_MINING` (Marinetti settled),
  guarded by a `g_netlogged` once-flag; (3) **trim `mlog_netinfo` to proven-safe calls only** —
  `TCPIPGetMyIPAddress` + `TCPIPGetConnectStatus`. `TCPIPGetConnectionMethod`/`GetMTU`/`GetDNS`
  returned garbage (`0x88888888` → `136.136.136.136`) and were removed. Net line is now clean
  (`NET ip=… up=Y`). Helper `dump_minerlog.sh` pulls both files off the image.

### 16d. Ship polish + real-pool defaults ✅ (`GSMINE82`→`88`)

- **Active-pool highlight (`GSMINE82`/`83`).** The main-grid `POOL` / `FAILOVER` rows now paint
  the **currently-mining pool in green** (value bright `H_GREEN`, label dark `H_GREEN_DK`,
  matching the live TCP lamp); the idle one stays cyan. Driven by `active_pool_idx()`
  (`g_demo`/`STR_MINING`/`strat_on_backup()`) and refreshed on every Stratum state change, so a
  failover flips the highlight live. Same text → an in-place recolor, no flicker.
- **No button flicker during polling (`GSMINE83`).** `draw_state()` (RUN/STOP/CONFIG) was
  redrawn on *every* poll transition; now it's gated on an actual `g_running` change, so the
  buttons stay rock-steady through subscribe/retry while badge/lamp/highlight still update.
- **Prefer-primary re-probe (`GSMINE84`).** While mining on the **backup**, a quiet timer
  (`REPROBE_SECS 180` = 3 min) does a **single-shot** re-test of the primary; if it lands a job
  we switch back, and if it doesn't, `fail_conn` **hops straight back to the working backup**
  (no failover-budget burn). Logged `REPROBE -> PRI` / `REPROBE miss -> BAK`. Disarmed while on
  primary. One trade-off: with a single socket the probe is briefly visible (badge/lamp blink).
- **Refined `CONN` logging (`GSMINE85`).** `FAIL CONN` now splits into `CONN-REFUSED` (fast
  SYN→RST: pool not accepting, server-side), `CONN-TIMEOUT` (no response in `OPEN_SECS`: route
  black-holed), and `CONN-DROPPED` (established session fell over) via a `g_conn_why` tag — so a
  reader can tell a dead pool from a dead route at a glance.
- **Real-pool ship defaults (`GSMINE86`/`88`).** Compiled defaults + RESTORE-DEFAULTS now ship
  **`SOLO.CKPOOL.ORG`** primary / **`PUBLIC-POOL.IO`** failover (was the Mac mock). ckpool is the
  battle-tested, long-running multi-node solo pool (rock-solid uptime, per-address dashboard);
  public-pool.io is the open-source community pool (nicer dashboard, single-node/flakier). For
  dev, point POOL at the mock (`192.168.2.1:3333`) by hand. Strings stored **uppercase** so the
  4×6 chassis font (which has a distinct lowercase `s` glyph) renders them consistently.
- **`HS_SECS` 8→12 (`GSMINE87`).** ckpool sometimes batches its first `mining.notify`; the 12 s
  handshake window rides that out instead of tripping `FAIL POOL` on startup.
- **`GSMINE69` pin retired.** Real-pool mining is now proven end-to-end, so the last
  OLD-toolchain A/B reference was unpinned (`PINS=""` in `inject_gsminer.sh`).
- **Clean-ship disk workflow.** A pristine shipped image = `GSMINE88` + `PANEL`/`CONFIG` plates
  + `FINDER.*`, with **no `MINER.LOG`/`MINER.OLD`/`MINER.CONF`**. First boot then comes up in
  **DEMO** with the real-pool defaults prefilled; flip to LIVE + Save to mine. Verified from a
  fresh boot: connected on **primary (`solo.ckpool.org`) first try**, clean `NET ip=…`, `STAT`
  heartbeat every 15 s, real `br=13` jobs, `need_bits=45`.
- **Easter egg (intentional).** The default wallet `3CfSNGtkdp…UQgKao` is the author's own
  **empty** BTC address — a deliberate gag (it will never mine anything). To be documented as
  intentional in the release README so contributors don't "fix" it.

### 16e. Build / files (current)

```
# large-model multi-file build (all six C units link against the patched SHA lib)
cd miner && occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c \
            -L. -llib65816hash -o viz && iix chtyp -t s16 viz
cd .. && ./inject_gsminer.sh -f            # injects GSMINE<NN> from APPVER; keeps current+previous

# pull the on-disk diagnostic logs
./dump_minerlog.sh                          # extracts MINER.LOG (+ MINER.OLD)
```

- New since M6: `miner/mlog.c` + `miner/mlog.h` (logger), `miner/cfg.c` + `miner/cfg.h`
  (config subsystem split out of `viz.c`), `dump_minerlog.sh`; offline harnesses
  `miner/brtest.c` (real-job stress), `miner/logtest.c` (stdio primitives), `miner/xmodtest.c`
  (SHA large-model repro).
- `inject_gsminer.sh` now has **no pins** (`PINS=""`); the rolling purge keeps only the current
  + previous rev. For a clean ship image, also delete `MINER.LOG`/`MINER.OLD`/`MINER.CONF`.

### 16f. Ship-readiness pass ✅ (`GSMINE89`→`90`, V0.89→V0.90)

- **`SYSFILES/` refactor** ✅ (`GSMINE89`, V0.89) — all runtime/system files moved off the volume
  root into `/MINERAPPS/SYSFILES/` so a first-time user sees a clean three-item disk:
  the app, the `SYSFILES` folder, and `Icons/`. Hardcoded paths bumped in `viz.c`
  (`PANEL_PATH`, `CONFIG_PATH`), `mlog.c` (`LOG_PATH`, `LOG_OLD`), `cfg.h` (`CONF_PATH`).
  `Icons/` MUST stay at the volume root — the Finder only scans `<vol>/Icons/` for `$CA` files.
  - Persistence (GS/OS): the Finder stores per-folder window + icon positions in an invisible
    `FINDER.DATA`. `make_master.sh` strips the *inherited* dev `FINDER.DATA`/`FINDER.ROOT` so a
    fresh disk auto-arranges a clean grid on first open; user moves persist normally thereafter
    (requires a writable, properly-ejected image). Verified: a gold-master run created
    `SYSFILES/MINER.CONF`, `MINER.LOG`, `MINER.OLD` + fresh root `FINDER.DATA`.
- **Custom Finder icon** ✅ — a `$CA` color icon (`Icons/GSMINER.ICONS`) mirroring the in-app
  Bitcoin coin (orange disc, black `B`). 640-mode desktop dithering forced a swatch-test to pick
  the right palette index (orange = index 6); large icon sized to 24×18 to avoid window clipping.
  Generator: `viz/build_icon.py` (emits the `.icons` file + a proof PNG).
- **Clean gold master** ✅ — `make_master.sh` clones a known-good 2mg wrapper, purges every old
  `GSMINE##` rev + dev clutter (`MINER.LOG/OLD/CONF`, root `PANEL/CONFIG`, stale `FINDER.DATA`),
  then lays down the app + `SYSFILES/{PANEL,CONFIG}` + `Icons/GSMINER.ICONS`.
- **Live-pool `JOBS` metric** ✅ (`GSMINE90`, V0.90) — the top-grid `BEST` cell was a local
  leading-zero high-water mark that freezes almost immediately at our hashrate. Replaced with
  `JOBS` = cumulative `mining.notify` work units the pool has pushed this session
  (`stratum.c` `g_jobs` / `strat_jobs()`). It moves on real network activity (new blocks +
  tx-set refreshes, ~30–60s) regardless of hashrate, and is cumulative so failover doesn't reset
  it. Demo mode shows `--` (no pool). The local best-bits metric still lives in the bottom-left
  scope well, so nothing is lost.

### 16g. Dashboard field reference (for the README)

Top grid — left column:

| Field | Shows | Source |
|---|---|---|
| `HASHRATE` | hashes/sec, compact (e.g. `6 H/S`) | real: hashes ÷ `GetTick` window |
| `SHARES`   | accepted / rejected | LIVE: `strat_accepted()/rejected()`; DEMO: local easy-target count |
| `JOBS`     | pool work units this session | LIVE: `strat_jobs()` (cumulative); DEMO: `--` |
| `UPTIME`   | session run time | `GetTick` since start (frozen when stopped) |

Top grid — right column:

| Field | Shows | Source |
|---|---|---|
| `NONCE`    | current 32-bit nonce under test | real loop counter |
| `HASHES`   | cumulative hashes computed | real |
| `NET DIFF` | network difficulty | LIVE: decoded from the job's `nbits` (`diff_from_nbits`); accurate but ~2-week-static. DEMO/pre-job: constant |
| `BLOCK`    | block height | LIVE: decoded from the coinbase via BIP34 (`height_from_coinb1`); climbs ~every 10 min. DEMO/pre-job: constant |

Bottom-left scope well:

| Field | Shows | Source |
|---|---|---|
| `ODDS`      | block-finding odds `1:N` | live projection from `NET_DIFF` (`--` when stopped) |
| `BEST`      | best leading-zero `BITS` this session | real local high-water (`g_bestbits`) |
| `SHA-256`   | live hash output hex | real last-hash bytes |
| `BLOCK ETA` | the punchline: time to find a block | live from `NET_DIFF` ÷ hashrate (the "trillions of years" gag) |

> README note: as of `GSMINE91` (V0.91) every LIVE readout is fed from real measurement or the
> live pool feed — including `NET DIFF` (decoded from `nbits`) and `BLOCK` (BIP34 height from the
> coinbase), which also feed the real ODDS/ETA numbers. `NET DIFF` is accurate but only retargets
> ~every 2 weeks, so it won't visibly move within a session; `BLOCK` ticks up ~every 10 min as new
> blocks land; `JOBS` ticks on every `mining.notify` (~1–2/min typical). DEMO mode and the brief
> LIVE pre-job window show illustrative constants / `--`.

### 16g-2. Live NET DIFF + BLOCK ✅ (`GSMINE91`, V0.91)

- **`NET DIFF`** — decoded from the current job's compact `nbits`
  (`diff = (0xFFFF / mantissa) * 2^(232 - 8*exp)`). Network-wide, retargets every 2016 blocks, so
  accurate-but-static within a session. Replaces the hardcoded `8.81e13`.
- **`BLOCK`** — block height parsed from the coinbase per BIP34 (`coinb1` byte 42 = height push
  length `N`, then `N` little-endian bytes). Climbs ~every 10 min on each new-block notify.
- Both decoders live in `viz.c` (`hexn`, `diff_from_nbits`, `height_from_coinb1`); the SI value
  formatter `fmt_si` ("88.1 T") was added to `numfmt.c`. The real difficulty now also feeds the
  ODDS and BLOCK-ETA punchlines, so the "trillions of years" gag computes off genuine numbers.
- **Perf regression + fix** ✅ (`GSMINE92`, V0.92): V0.91 ran `diff_from_nbits` (a ~48-step SANE
  software-`double` loop) + the BIP34 `strlen`/parse on **every ~0.5s redraw**. The cooperative
  main loop is `BATCH=1` (one SHA/iteration) and the draw path is already SANE-float-bound, so
  this collapsed real-pool hashrate from ~8 H/s to ~1 H/s. Fix: cache the decoded diff + height
  keyed on `job->gen` (re-decode only when a new `mining.notify` arrives, a few times a minute),
  so the heavy float runs ~2×/min instead of ~2×/s. **Lesson banked: keep SANE `double` work out
  of per-frame paths on the 65816 — cache anything that doesn't change every frame.**

### 16h. Remaining before public release (2026-06-01)

| Item | Status |
|------|--------|
| **Shipping build** | **`GSMINE95` / V0.95** — beta gold master; multi-hour soaks stable; first live mining on real hardware |
| **User README** | ✅ LIVE vs DEMO, submit filtering, hash lottery / turbo-GS note, CONFIG, mock pool, credits |
| **`RELEASE_COMMS.md`** | ✅ FB post draft, GitHub release notes, repo layout checklist |
| **In-app logo / branding** | ✅ gold GS / red MINER blitted at runtime into the SHR well + dynamic version stamp (`GSMINE94`, §16k) |
| **Gold-master disk → GitHub Release** | ⬜ `make_master.sh` + attach `.2mg` (not dev `apps.2mg`) |
| **LICENSE** | ✅ MIT (`LICENSE`) + ISC note for vendored `65816-crypto/` |
| **Source drop** | ⬜ few days after disk — `miner/`, patched `65816-crypto/`, build scripts, trimmed docs |
| **M-accel (TWGS/ZipGS)** | ⬜ optional real-iron hashrate multiplier |
| **Draw-path perf** | ⬜ optional; V0.95 already ~9–10 H/s on stock 2.8 MHz |

**ckpool worker UI lag:** wallet dashboard may not show the worker immediately; in-app `SHA-256D` +
`JOBS` ticking = connected. Same reported for Bitaxe/Nano II.

### 16i. Submit filtering — "is this really mining?" (2026-06-01)

**Short answer:** LIVE mode hashes **real** pool work. SHARES often stay `0 / 0` on public pools
because the **share target** is unreachable at ~10 H/s — not because the app refuses to mine.

**Mechanism (`mine.c` + `stratum.c`):**
- Each nonce: `sha256d_header()` → count **leading zero bits** (`zbits_of`).
- LIVE submit iff `zbits >= strat_need_bits()`.
- **Public hostname:** `strat_need_bits()` = pool's `mining.set_difficulty` mapped to bits
  (`bits_for_diff`: diff ≥ 1 → **≥ 32 bits**; ckpool soaks saw **~45 bits**).
- **Private/LAN IP** (`is_private_host`): override to **8 bits** (mock/demo ACCEPT path).

**There is no** `if (public_pool) never submit`. A hash that clears the pool bar **always**
submits — on stock iron, turbo emulator (~1 kH/s at hypothetical 300 MHz linear scaling), or
future accelerated hardware.

**Expected tries (geometric mean ≈ 2^N):**

| Target | ~Hashes | ~10 H/s | ~1 kH/s (turbo GS*) |
|--------|---------|---------|---------------------|
| 32 bits | 4.3×10⁹ | ~14 years | ~50 days |
| 45 bits | 3.5×10¹³ | ~110k years | ~1.1k years |

\*Rough linear clock scaling only; not a measured build.

**Why filter weak hashes:** submitting 8BITCOIN-style "interesting" shares (few zero bytes) would
flood the pool with **rejects** (`result: false`) and risk rate-limit / disconnect. We filter shares
**we know will fail** — hygiene, not fake mining.

**User doc:** full prose in root **`README.md`** ("Is this really mining?"). Comms summary in
**`RELEASE_COMMS.md`** internal notes.

### 16j. V0.93 perf note

After the V0.92 draw-path cache fix (`diff_from_nbits` / BIP34 height keyed on `job->gen`),
measured hashrate on stock 2.8 MHz returned to **~9–10 H/s** (up from ~1 H/s regression in V0.91).
`APPVER "V0.93"` in `miner/viz.c`; inject as `GSMINE93` via `inject_gsminer.sh`.

### 16k. In-app logo — gold GS / red MINER (V0.94, 2026-06-02)

The logo well used to draw plain `text(28,16,"GS MINER")`. V0.94 renders the **approved chiseled
wordmark** at runtime: a **gold (idx 14) `GS` monogram + red (idx 13) `MINER`**, black-outlined with
a grey chrome bevel, plus a **dynamic lowercase `v0.94` version stamp**.

**Pipeline (single source of truth):**
- `viz/logo_depth_proof.py` `render_well_idx()` rasterises the wordmark to **SHR header-band palette
  indices** (0=black, 1–8 grey ramp, 13=red, 14=gold, 15=white; 255 = transparent) and emits
  **`miner/logo_gs.h`** (`LOGO_GS[]`, plus `LOGO_GS_X/Y/W/H`). No version baked in.
- `viz.c` `draw_logo()` blits every non-255 index via `setpix()` into the well at `draw_static`
  time, then `text_r()` stamps the dynamic `APPVER` on top (so a rev bump updates it for free).

**Palette reality:** the header band has flat gold/red but **no dark-gold/dark-red**, so depth comes
from the black outline + grey bevel rather than a colour gradient (the Mac RGB proof is richer; the
on-iron blit is intentionally flatter). MINER body is solid red (earlier two-tone grey "banding"
removed).

**Frame change:** the logo well was widened **82 → 85 px** (`build_frame.py`, right edge 96 → 99 into
the metal gap before the field groove at x=100) so MINER's `R` clears the right border at natural
spacing. `frame.shr` (PANEL) regenerated + re-injected.

**Lowercase `v`:** `font_gs.h` is uppercase-only (`FONT_HI=94`), and `APPVER` must stay `"V0.94"`
(the `inject_gsminer.sh` rev parser matches `"V0\.N"`). So `drawchar()` special-cases a baseline-
aligned lowercase `v` glyph (borrowed from `font4x6.py`) and `draw_static` lowercases only the
**display** string. `APPVER "V0.94"` → inject as `GSMINE94`.

### 16l. Real-hardware bring-up: portable paths + 2IMG header (V0.94, 2026-06-02)

First CFFA3000 / real-iron test surfaced two ship blockers (both passed fine under Ample, which is
lenient). Fixed for V0.94:

**1. `apps.2mg` rejected by CFFA as "invalid image."** The dev pipeline clones a *headerless*
800K ProDOS-order image but names it `.2mg`. Emulators auto-detect by content; **CFFA requires the
4-byte `2IMG` magic header** for a `.2mg`. Proof: `apps.2mg` starts `01 38 B0 03` (ProDOS boot block,
no header, exactly 819200 = 1600×512 bytes); `miner_boot.2mg` starts `2IMG` and mounts.
- **Fix:** `wrap_2mg.py` prepends a correct 64-byte 2IMG header (format=1 ProDOS order, 1600 blocks).
  `make_master.sh` now emits the shippable **`gsminer_v0.<rev>.2mg`** (headered, clean: only the app +
  `SYSFILES/` + `Icons/`, no `FINDER.DATA`/old revs). The raw `apps.2mg` stays headerless for the
  AppleCommander+Ample dev loop.

**2. App died at launch: `cannot open /MINERAPPS/SYSFILES/PANEL`** when the three parts were copied
into a *folder* named MINERAPPS (vs. a *volume* named MINERAPPS). The old code hard-coded absolute
paths, which only resolve when the volume itself is MINERAPPS.
- **Fix:** new **`miner/paths.c`** (`paths_init()` + `miner_sysfile()`). Data dir resolves to
  **`1/SYSFILES/`** (GS/OS prefix 1 = the directory the app was launched from → copy-anywhere), with a
  fallback to **`/MINERAPPS/SYSFILES/`** so the shipped volume still works. Probed once at startup
  (does `<base>PANEL` open?) and cached; `viz.c`/`cfg.c`/`mlog.c` all route file I/O through it. A ring
  of static result buffers keeps `rename(LOG, OLD)` valid. New build line adds `paths.c`.

**Known caveat — Finder icons are volume-scoped.** GS/OS Finder loads icon files only from a volume's
root `Icons/` folder, so `GSMINER.ICONS` shows only when it sits at the **volume root** (true on the
`/MINERAPPS` disk). Copy the parts into a *subfolder* and the app still runs (data is now relative) but
the custom coin icon won't appear unless `GSMINER.ICONS` is also placed in that volume's root `Icons/`.
This is inherent Finder behaviour, not a bug.

### 16m. 🏆 FIRST LIVE MINING ON REAL HARDWARE (V0.95, 2026-06-03)

**It mined.** On real Apple IIgs iron — a **14 MHz TWGS**-accelerated machine with an **Uthernet II in
slot 2**, GS/OS 6.0.4 + Marinetti — GS Miner connected to **`solo.ckpool.org`** and ran the full live
Stratum loop against real `mining.notify` jobs. Filed historically somewhere between Bell's first
phone call and Doc Brown's flux-capacitor sketch. 🛹⚡

**The fix that got it there (V0.95):** the LIVE mode error on iron was **"MARINETTI NOT FOUND"** even with
a confirmed-connected stack. Root cause: the app assumed Marinetti's TCP/IP tool set (tool $36 / `TOOL054`)
was already resident (true in the Ample boot), so on hardware where it wasn't loaded into *our* context,
every `TCPIP*` call returned `toolNotFoundErr`. Fix = `cfg_net_bringup()` in `cfg.c`: `LoadOneTool(54, …)`
+ `TCPIPStartUp()` at launch, with the tool-error codes logged to `MINER.LOG`. **Not a slot issue** — the
Uthernet II slot is purely Marinetti's link-layer config; the app never references a slot.

**Validation — the proof session (`MINER.LOG`, lightly trimmed):**

```text
T+0.86  NET loadtool err=0000          ← tool set loaded (the V0.95 fix)
T+0.98  NET startup  err=0000          ← TCPIPStartUp OK
T+1.08  NET probe connstat=Y err=0000  ← stack visible + connected
T+34.05 CONNECT SOLO.CKPOOL.ORG:3333 [PRI]
T+36.21 DNS= SOLO.CKPOOL.ORG -> 15.204.102.129
T+36.53 TCP-UP SOLO.CKPOOL.ORG
T+37.45 SUBOK en1=9204526a en2sz=8     ← ckpool accepted subscribe
T+37.58 DIFF need_bits=45              ← pool share target = 45 zero-bits
T+37.73 JOB 6a1e810000000a51 …nbits=1702068f
T+38.00 MINING [PRI] dev=0
T+38.11 NET ip=192.168.4.41 up=Y
…STAT hr=18→23→21→20→22→22  best=12  acc=0 rej=0
T+128.18 JOB …a54 (4th live job; ntime ticking +30s each)
T+136.11 BYE                           ← clean shutdown
```

**What the log confirms:**
- Real end-to-end pipeline: DNR resolve → TCP → `subscribe`/`authorize` → `SUBOK` (extranonce1
  `9204526a`, extranonce2 size 8) → live `mining.notify` jobs (`a51`→`a54`, `ntime` +30s each) →
  SHA-256d on real 80-byte headers.
- **Hashrate ~21 H/s** on the 14 MHz TWGS (vs ~9–10 H/s on stock 2.8 MHz).
- **Submit gating correct on a public pool:** target **45 bits**, session best **12 bits** → **never
  submitted** → `acc=0 rej=0` (zero rejects; we never spammed ckpool). Hygiene, not "fake mining."
- **Statistical sanity:** ~21 H/s × ~95 s ≈ 2,000 hashes ⇒ expected best ≈ log₂(2000) ≈ 11 bits; observed
  **12**. Dead-on the probability curve — the hashing is sound, not stuck/looping.
- Clean teardown (`BYE` → `mlog_close`), no hang/crash.

**Log cosmetic (V0.95):** the startup probe now logs `connstat=Y/N` instead of the raw Marinetti TRUE
word (`0x8000` printed as `-32768`).

**Portability + finished-look (workflow, applies to the next master):** `miner/paths.c` resolves data
app-relative (`1/SYSFILES/`) with `/GSMINER/SYSFILES/` and `/MINERAPPS/SYSFILES/` fallbacks; `make_master.sh`
now renames the shipped ProDOS volume to **`GSMINER`** (cosmetic, thanks to the fallback list) and wraps
a real **2IMG** header so the disk mounts on **CFFA3000** (a bare headerless `.2mg` was rejected as
"invalid image" on iron — see §16l).

### 16n. Final "cowboy pass" — perf reality + optimization roadmap (V0.95, 2026-06-03)

Before locking V0.95 we did a deliberate top-to-bottom review with one question: *what will the
65816 console cowboys (the 8BITCOIN / Brutal Deluxe crowd) zero in on, and where's the real
headroom?* The honest answer reshaped the roadmap.

**Perf reality — we are compression-bound, and the core is already tuned.** Reviewed the full hot
path: `mine.c` (`sha256d_header` midstate fast path), Heumann's `sha256.asm` + `sha256.macros`, and
the `viz.c` main loop. Double-SHA-256 is *fundamentally* two full 64-byte compressions per nonce,
and Heumann's compression is already a top-tier 65816 implementation — DP-resident state with
per-word `zero` guard slots for shift-across rotates, precomputed even-index constants
(`two`…`thirty`), the 8-way-unrolled `BlockLoopIter` that rotates *which DP slot* is `a..h` rather
than moving data, schedule computed 16 words at a time. Cycle check against measured numbers: at
~9–10 H/s stock, each hash ≈ 280k cycles, essentially all of it inside those two compressions.

| Lever | Realistic gain | Verdict |
|---|---|---|
| C-side cleanup (pre-padded 2nd block instead of generic `init`/`update`/`finalize`; drop the redundant LE→BE→LE swap; stop re-zeroing block-2 each nonce) | **< 1%** | **Skip.** Pure hygiene; disturbs the path proven bit-identical by `mstest.c`/`mock_pool.py` for ~0 gain. |
| asm nonce-specialization (block-1 midstate *past round 2* — nonce is `W3`, so rounds 0–2 + schedule `W0–W17` are job-constant; fold `K+W` on the ~23 constant-`W` rounds; final-word early-out) | **~2–5%** | **Table.** Real cowboy work, bragging rights only — not a multiplier. |
| **CPU clock** | **linear** | **The only real lever.** ~10 H/s stock → ~21 H/s @ 14 MHz TWGS (~2×) → ZipGS/turbo scales ~linearly. |

Contrast with 8BITCOIN: that "exponential" win came from a cowboy rewriting a *naïve* 6502 SHA.
Ours started from a tuned library, so the asymptote is already close. Stating this in the public
write-up (WRITEUP §18) pre-empts the "why isn't this 10× faster?" question with the real reason.

**Code-health verdict — ship the core as-is.** The hash path is correct (midstate proven by
`miner/mstest.c`), and the entire Stratum→header buffer chain is provably bounded: `next_str()`
truncates (`if (n < outsz - 1)`), each parse temp matches its `g_job` field size
(`c1[512]→coinb1[512]`, branch `[68]`), `g_hexcat[1152]` ≥ the 1088-char max concatenation, and
`coinbase[640]` ≥ the 544-byte worst case. **No overflow, no change recommended to the V0.95 core.**

**Where the payoff actually is (deferred features, not speed):**
- **Ensoniq DOC soundtrack** — a looping SoundSmith/multivoice track + a soft per-*N*-hash
  tick, behind a **global MUTE**. Headliner delight. Patterns: `reference/antoinevignau-source/ensoniq/`.
- **`sha256d-65816` mini-library** — package the proven double-SHA core (midstate + endianness
  handling) as a clean reusable 65816 unit. Community contribution.
- **Novel 3200 hash visualizer** — *only if functional*: a full-screen "screensaver" where the live
  digest stream paints evolving 3200-colour art (PicViewer technique). A static boot splash that
  flips into 16-colour ops would feel weak; a hash-driven visualizer is novel.

**Explicitly dropped (with rationale):**
- **"Share found" fanfare** — unreachable target ⇒ it would *never* fire; dead code.
- **CPU-speed auto-detect / H-per-MHz readout** — TWGS/ZipGS/AppleSqueezer/emulator-turbo matrix
  makes a reliable reading more trouble than it's worth.
- **Best-bits leaderboard** — realistic user count too small to matter.
