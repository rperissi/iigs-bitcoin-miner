#!/usr/bin/env python3
"""
mock_pool.py - a tiny, faithful Stratum v1 mining pool for developing the
Apple IIgs miner against Ample (or real iron) on your Mac.

WHY THIS EXISTS
---------------
Real pools hand out work at real (impossible) difficulty, so you'd never see
the GS "find" anything and couldn't tell if its hashing is even correct. This
mock pool instead:
  * speaks real Stratum v1 (mining.subscribe / authorize / notify / submit),
  * hands out a FIXED, fully-specified job at an ABSURDLY EASY target so the GS
    finds "shares" in seconds,
  * RE-VERIFIES every share the GS submits by recomputing SHA256d in Python,
    so it doubles as the correctness oracle for the GS SHA-256 + header code.

It reuses the exact network path we already proved working today: the GS dials
OUT to the Mac at 192.168.2.1 (vmnet gateway). One long-lived TCP socket, line-
delimited JSON - structurally identical to the FTP control channel that worked.

RUN ON THE MAC:
    python3 mock_pool.py
Then point the GS miner at host 192.168.2.1, port 3333.

----------------------------------------------------------------------------
THE 80-BYTE BLOCK HEADER THE GS MUST BUILD (this is the implementation spec)
----------------------------------------------------------------------------
All multi-byte integers below are stored LITTLE-ENDIAN in the header bytes.

  offset  size  field         source
  ------  ----  -----------   ----------------------------------------------
   0      4     version       notify param, as LE int
   4     32     prev block    notify param; per Stratum, each of the 8 32-bit
                              words is byte-reversed (see swap_prevhash()).
                              (Our demo prevhash is all zeros, so this is a
                              no-op for milestone 1 - byte order can't bite you
                              until you point at a real pool.)
  36     32     merkle root   SHA256d(coinbase), then folded through the
                              merkle_branch list (empty in our demo, so the
                              merkle root == the coinbase hash).
  68      4     ntime         notify param, as LE int (may be rolled)
  72      4     nbits         notify param, as LE int
  76      4     nonce         <-- THE GS ITERATES THIS, as LE int

  coinbase = coinb1 + extranonce1 + extranonce2 + coinb2   (all hex -> bytes)

  blockhash = SHA256( SHA256( header ) )

A share is "good" when blockhash, read as a little-endian 256-bit integer, is
<= TARGET. We set TARGET very high (easy) so it happens fast.
----------------------------------------------------------------------------
"""
import json
import socket
import struct
import threading
import datetime
from hashlib import sha256

HOST = "0.0.0.0"     # binds 192.168.2.1 too; GS connects to 192.168.2.1
PORT = 3333

# Extranonce handed to the client at subscribe time.
EXTRANONCE1 = "f0f0f0f0"
EXTRANONCE2_SIZE = 4

# --- The demo job -----------------------------------------------------------
# Kept deliberately simple so milestone 1 has zero endianness traps:
#   * prevhash all zeros  -> word-swap is a no-op
#   * empty merkle_branch -> merkle root == coinbase hash
JOB = {
    "job_id":   "demo1",
    "prevhash": "00" * 32,
    "coinb1":   "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff",
    "coinb2":   "ffffffff0100f2052a01000000000000000000",
    "merkle_branch": [],
    "version":  "20000000",
    "nbits":    "1d00ffff",
    "ntime":    "5e9f1a00",
    "clean":    True,
}

# --- Difficulty / acceptance target ----------------------------------------
# EASY_ZERO_BYTES = how many leading zero BYTES the blockhash (big-endian view)
# must have to be accepted. 1 => ~256 hashes average (instant). Bump to 2 for
# ~65k (a slower, more "worky" demo). This is our toy target, not real network
# difficulty.
EASY_ZERO_BYTES = 1
TARGET = (1 << (8 * (32 - EASY_ZERO_BYTES))) - 1   # blockhash_int <= TARGET


def log(msg):
    print(f"[{datetime.datetime.now():%H:%M:%S}] {msg}", flush=True)


def sha256d(b: bytes) -> bytes:
    return sha256(sha256(b).digest()).digest()


def swap_prevhash(prevhash_hex: str) -> bytes:
    """Stratum prevhash -> header bytes: reverse each of the eight 32-bit words."""
    raw = bytes.fromhex(prevhash_hex)
    out = bytearray()
    for i in range(8):
        out += raw[i * 4:(i + 1) * 4][::-1]
    return bytes(out)


def build_header(job, extranonce2_hex, ntime_hex, nonce_int) -> bytes:
    coinbase = bytes.fromhex(
        job["coinb1"] + EXTRANONCE1 + extranonce2_hex + job["coinb2"]
    )
    merkle_root = sha256d(coinbase)
    for branch in job["merkle_branch"]:
        merkle_root = sha256d(merkle_root + bytes.fromhex(branch))

    header = b""
    header += struct.pack("<I", int(job["version"], 16))
    header += swap_prevhash(job["prevhash"])
    header += merkle_root
    header += struct.pack("<I", int(ntime_hex, 16))
    header += struct.pack("<I", int(job["nbits"], 16))
    header += struct.pack("<I", nonce_int & 0xFFFFFFFF)
    assert len(header) == 80, len(header)
    return header


def verify_share(extranonce2_hex, ntime_hex, nonce_int):
    """Recompute the blockhash for a submitted share. Returns (ok, zero_bytes, hexhash)."""
    header = build_header(JOB, extranonce2_hex, ntime_hex, nonce_int)
    h = sha256d(header)
    val = int.from_bytes(h, "little")               # little-endian 256-bit int
    big = h[::-1]                                    # big-endian for human display
    zero_bytes = 0
    for byte in big:
        if byte == 0:
            zero_bytes += 1
        else:
            break
    return (val <= TARGET, zero_bytes, big.hex())


def send(conn, obj):
    line = (json.dumps(obj) + "\n").encode()
    conn.sendall(line)


def send_job(conn):
    # We advertise difficulty 1 like a normal pool, but actually ACCEPT at the absurdly
    # easy EASY_ZERO_BYTES target so the GS finds shares in seconds. The GS knows to use
    # its easy submit target here because we're on a private/LAN address (192.168.x) - it
    # only gates submissions on set_difficulty for real, public pools. So this value is
    # cosmetic for the dev loop; the GS keys "dev mode" off the 192.168.2.1 address.
    send(conn, {"id": None, "method": "mining.set_difficulty", "params": [1]})
    send(conn, {
        "id": None,
        "method": "mining.notify",
        "params": [
            JOB["job_id"], JOB["prevhash"], JOB["coinb1"], JOB["coinb2"],
            JOB["merkle_branch"], JOB["version"], JOB["nbits"], JOB["ntime"],
            JOB["clean"],
        ],
    })
    log(f"sent job '{JOB['job_id']}' (target: blockhash needs >= {EASY_ZERO_BYTES} "
        f"leading zero byte(s))")


def handle(conn, addr):
    log(f"CONNECT from {addr[0]}:{addr[1]}")
    shares = 0
    buf = b""
    try:
        with conn:
            while True:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        msg = json.loads(line)
                    except ValueError:
                        log(f"  <- non-JSON line: {line!r}")
                        continue
                    mid = msg.get("id")
                    method = msg.get("method")
                    params = msg.get("params", [])
                    log(f"  <- {method} {params}")

                    if method == "mining.subscribe":
                        send(conn, {
                            "id": mid, "result": [
                                [["mining.set_difficulty", "1"],
                                 ["mining.notify", "1"]],
                                EXTRANONCE1, EXTRANONCE2_SIZE,
                            ], "error": None,
                        })
                    elif method == "mining.authorize":
                        send(conn, {"id": mid, "result": True, "error": None})
                        send_job(conn)            # start feeding work once authorized
                    elif method == "mining.submit":
                        # params: [worker, job_id, extranonce2, ntime, nonce]
                        try:
                            _, job_id, en2, ntime, nonce_hex = params[:5]
                            nonce = int(nonce_hex, 16)
                            ok, zb, hexhash = verify_share(en2, ntime, nonce)
                        except Exception as e:
                            log(f"  !! bad submit ({e})")
                            send(conn, {"id": mid, "result": False,
                                        "error": [20, "bad submit", None]})
                            continue
                        shares += 1
                        verdict = "ACCEPTED" if ok else "REJECTED (hash too high)"
                        send(conn, {"id": mid, "result": bool(ok), "error": None})
                        log(f"  ** SHARE #{shares} {verdict}  nonce={nonce:#010x} "
                            f"zero_bytes={zb}  hash={hexhash}")
                    else:
                        # be permissive about anything else (suggest_difficulty, etc.)
                        if mid is not None:
                            send(conn, {"id": mid, "result": True, "error": None})
    except ConnectionError as e:
        log(f"  connection error: {e}")
    log(f"closed {addr[0]} (received {shares} share(s))")


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((HOST, PORT))
    srv.listen(5)
    log(f"mock Stratum pool listening on {HOST}:{PORT}")
    log(f"point the GS miner at host 192.168.2.1 port {PORT}")
    log(f"easy target = {EASY_ZERO_BYTES} leading zero byte(s)  (TARGET={TARGET:#x})")
    # sanity self-test: prove the verifier works by brute-forcing a share here.
    selftest()
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=handle, args=(conn, addr), daemon=True).start()


def selftest():
    """Brute-force a valid nonce in Python to confirm the job is solvable & the
    verifier is correct - this is the exact computation the GS must reproduce."""
    en2 = "00000000"
    for nonce in range(0, 1 << 24):
        ok, zb, hexhash = verify_share(en2, JOB["ntime"], nonce)
        if ok:
            log(f"selftest: solvable - nonce={nonce:#010x} gives {zb} zero byte(s), "
                f"hash={hexhash}")
            log(f"selftest: (the GS should find a share like this within ~{1<<(8*EASY_ZERO_BYTES)} tries)")
            return
    log("selftest: WARNING - no share found in 2^24 tries; target too hard for demo")


if __name__ == "__main__":
    main()
