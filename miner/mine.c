/*
 * mine.c - SHA-256d mining core for the IIgs dashboard, split out of viz.c for
 * modularity. The project is built with the LARGE memory model (occ -b) so the combined
 * code can span multiple 64KB banks. The mining STATE lives in viz.c (root data) and is
 * shared via mine.h; this file holds only the compute (header build, midstate fast path,
 * the nonce-grinding slice). DEMO and LIVE both run through here.
 *
 * NOTE (the GSMINE76 hash-corruption fix): the SHA library (lib65816hash) was originally
 * NOT large-model safe - sha256.asm reads its round-constant table relative to the data
 * bank register, which under occ -b pointed at a different bank than the relocated
 * constants, so EVERY hash was silently wrong (DEMO never noticed - it has no oracle -
 * but the mock/real pool rejected our shares). Fixed in 65816-crypto/sha256.asm by
 * keeping the constants in the PROCESSBLOCK segment and forcing DBR to that bank for the
 * block; the bundled lib65816hash is rebuilt from it. So -b is safe again.
 *
 * Build: occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c -L. -llib65816hash -o viz
 */
#include <string.h>
#include "sha256.h"
#include "stratum.h"
#include "mine.h"

/* ---- the demo job: a fixed, fully-specified header the local SHA-256d sweeps ---- */
static const char COINBASE_HEX[] =
    "01000000010000000000000000000000000000000000000000000000000000000000000000ffffffff"
    "f0f0f0f0" "00000000" "ffffffff0100f2052a01000000000000000000";
#define VERSION 0x20000000UL
#define NTIME   0x5e9f1a00UL
#define NBITS   0x1d00ffffUL
#define BATCH   1              /* nonces per slice: 1 so mouse/keys poll every hash (~18Hz) */

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return 0;
}

static unsigned hex2bytes(const char *hex, unsigned char *out)
{
    unsigned i = 0;
    while (hex[i * 2] && hex[i * 2 + 1]) {
        out[i] = (unsigned char)((hexval(hex[i * 2]) << 4) | hexval(hex[i * 2 + 1]));
        i++;
    }
    return i;
}

static void put_le32(unsigned char *p, unsigned long v)
{
    p[0] = (unsigned char)v;        p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void sha256d(const unsigned char *data, unsigned long len, unsigned char out[32])
{
    unsigned char first[32];
    sha256_init(g_ctx); sha256_update(g_ctx, data, len); sha256_finalize(g_ctx);
    memcpy(first, g_ctx->hash, 32);
    sha256_init(g_ctx); sha256_update(g_ctx, first, 32UL); sha256_finalize(g_ctx);
    memcpy(out, g_ctx->hash, 32);
}

/* M5 midstate: header[0..63] (inner-hash block 1) is constant while the nonce
 * sweeps, so compress it once per job and cache the 8-word state. Call this
 * whenever g_header[0..63] changes (new DEMO or LIVE job). The lib's state words
 * are little-endian during computation (finalize swaps to BE at the very end),
 * so we snapshot context->hash raw and byte-swap the inner digest ourselves.
 * Verified equivalent to whole-message sha256d offline (miner/mstest.c). */
static void midstate_update(void)
{
    sha256_init(g_ctx);
    memcpy(g_ctx->block, g_header, 64);
    sha256_processblock(g_ctx);
    memcpy(g_midstate, g_ctx->hash, 32);
}

/* SHA-256d of g_header using the cached midstate: ~2 compressions/nonce (vs 3). */
static void sha256d_header(unsigned char out[32])
{
    unsigned char inner[32];
    unsigned char *blk = g_ctx->block;
    int w, b;
    memcpy(g_ctx->hash, g_midstate, 32);          /* restore block-1 state */
    memcpy(blk, g_header + 64, 16);               /* block 2 = header[64..79] + pad */
    memset(blk + 16, 0, 48);
    blk[16] = 0x80;
    blk[62] = 0x02; blk[63] = 0x80;               /* 640-bit message length, big-endian */
    sha256_processblock(g_ctx);
    for (w = 0; w < 8; w++)                        /* LE state words -> BE inner digest */
        for (b = 0; b < 4; b++)
            inner[w * 4 + b] = g_ctx->hash[w * 4 + (3 - b)];
    sha256_init(g_ctx); sha256_update(g_ctx, inner, 32UL); sha256_finalize(g_ctx);
    memcpy(out, g_ctx->hash, 32);
}

/* leading zero BITS of the big-endian hash (digest[31] is the MSB) */
static int zbits_of(const unsigned char *d)
{
    int i = 31, zb = 0, bits;
    unsigned char c;
    while (i >= 0 && d[i] == 0) { zb++; i--; }
    bits = zb * 8;
    if (i >= 0) { c = d[i]; while ((c & 0x80) == 0) { bits++; c <<= 1; } }
    return bits;
}

void mine_live_reset(void)
{
    g_live_gen = 0;
}

void build_job(void)
{
    unsigned char coinbase[128], merkle[32];
    unsigned cb = hex2bytes(COINBASE_HEX, coinbase);
    sha256d(coinbase, (unsigned long)cb, merkle);
    memset(g_header, 0, 80);
    put_le32(g_header + 0, VERSION);
    memcpy(g_header + 36, merkle, 32);
    put_le32(g_header + 68, NTIME);
    put_le32(g_header + 72, NBITS);
    midstate_update();                      /* M5: cache block-1 state for this job */
}

static unsigned long hex2u32(const char *s)
{
    unsigned long v = 0;
    while (*s) { v = (v << 4) | (unsigned long)hexval(*s); s++; }
    return v;
}

/* build the 80-byte header from a LIVE pool job (same layout as miner.c M3 and
 * mock_pool.py build_header): coinbase = coinb1 + extranonce1 + extranonce2 + coinb2;
 * merkle root = dSHA(coinbase) folded through the merkle_branch (empty on the mock,
 * non-empty on a real pool); prevhash byte-reversed per 32-bit word into the header. */
static void build_job_live(const StratJob *j)
{
    /* static, not stack: ~512 bytes of scratch off the 65816 stack. Single-threaded, so
     * sharing is safe. (This was never the share-reject cause - that was the SHA library
     * under -b; see the header note - but keeping big temps off the stack is good practice
     * here and matches process_line() in stratum.c.) */
    static unsigned char coinbase[640], merkle[32], bb[64], raw[32];
    unsigned cb;
    int w, b, k;
    g_hexcat[0] = '\0';
    strcat(g_hexcat, j->coinb1);
    strcat(g_hexcat, j->en1);
    strcat(g_hexcat, j->en2);                /* extranonce2, sized to the pool's request */
    strcat(g_hexcat, j->coinb2);
    cb = hex2bytes(g_hexcat, coinbase);
    sha256d(coinbase, (unsigned long)cb, merkle);
    for (k = 0; k < j->nbranch; k++) {       /* merkle = dSHA(merkle || branch[k]) */
        memcpy(bb, merkle, 32);
        hex2bytes(j->branch[k], bb + 32);
        sha256d(bb, 64UL, merkle);
    }
    memset(g_header, 0, 80);
    put_le32(g_header + 0, hex2u32(j->version));
    hex2bytes(j->prevhash, raw);
    for (w = 0; w < 8; w++)
        for (b = 0; b < 4; b++)
            g_header[4 + w * 4 + b] = raw[w * 4 + (3 - b)];   /* per-word byte swap */
    memcpy(g_header + 36, merkle, 32);
    put_le32(g_header + 68, hex2u32(j->ntime));
    put_le32(g_header + 72, hex2u32(j->nbits));
    midstate_update();                      /* M5: cache block-1 state for this job */
}

/* hash one slice of BATCH nonces, update stats. In LIVE we mine the pool's job
 * (rebuilding the header whenever a new mining.notify arrives) and submit each hit
 * the pool will accept; the pool's verdict feeds the accepted-share count. */
void mine_slice(void)
{
    unsigned char hash[32];
    int b, zbits, i;
    if (!g_demo) {
        const StratJob *j = strat_job();
        if (j->gen == 0 || !strat_havesub()) return; /* need subscribe en1 + a notify job */
        if (j->gen != g_live_gen) {                  /* new job / forced rebuild: refresh header */
            build_job_live(j);
            g_live_gen = j->gen;
            g_nonce    = 0;
        }
    }
    for (b = 0; b < BATCH; b++) {
        put_le32(g_header + 76, g_nonce);
        /* M5 midstate fast path (~1.5x), DEMO and LIVE alike: block-1 (header[0..63])
         * is cached per job by build_job()/build_job_live(); only the nonce in block 2
         * changes here. Proven bit-identical to whole-message sha256d (miner/mstest.c),
         * so a zbits hit here is a real hit - we can submit it directly. */
        sha256d_header(hash);
        g_hashes++;
        zbits = zbits_of(hash);
        if (zbits > g_bestbits) g_bestbits = zbits;
        if (g_demo) {
            if (zbits >= 8) g_shares++;              /* DEMO: fixed easy target, local count */
        } else if (zbits >= strat_need_bits()) {     /* LIVE: hit clears the pool target */
            strat_submit(g_nonce);
        }
        g_nonce++;
    }
    for (i = 0; i < 32; i++) g_last[i] = hash[i];    /* latest digest for display */
    for (i = 0; i < 6; i++) {                        /* feed scope ring (BE bytes) */
        g_ring[g_rhead] = g_last[31 - (i & 31)];
        g_rhead = (g_rhead + 1) % SW;
    }
}
