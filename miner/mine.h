/*
 * mine.h - interface between the dashboard (viz.c) and the SHA-256d mining core
 * (mine.c). The two were split for modularity; the project is built with the LARGE
 * memory model (occ -b) so the combined code can span multiple 64KB banks.
 *
 * The mining STATE lives in viz.c (root data) and is shared here; mine.c only holds
 * the compute. Keep these extern decls in step with the (now non-static) definitions
 * in viz.c.
 */
#ifndef __MINE_H__
#define __MINE_H__

/* sha256.h (no include guard) must be included by the .c before this header. The
 * extern below only needs the struct TAG, so we forward-reference it here. */
#define SW 138                 /* scope ring width (shared: g_ring[] + draw_scope) */

extern struct sha256_context *g_ctx;
extern unsigned char  g_header[80];
extern unsigned char  g_midstate[32];   /* M5: cached inner-hash state after header[0..63] */
extern char           g_hexcat[1152];    /* coinbase hex scratch (LIVE header build) */
extern unsigned long  g_live_gen;        /* pool job gen the LIVE header was built from */
extern unsigned long  g_hashes, g_shares, g_nonce;
extern int            g_bestbits;
extern unsigned char  g_last[32];        /* most recent digest (hash line / scope) */
extern unsigned char  g_ring[SW];        /* scope ring */
extern int            g_rhead;
extern int            g_demo;            /* 1 = local SHA demo; 0 = live Stratum */

/* DEMO: rebuild the fixed demo header. LIVE: mine_slice() pulls the pool job itself. */
void build_job(void);
/* hash one BATCH slice, update stats; DEMO counts shares locally, LIVE submits to the pool. */
void mine_slice(void);
/* Force the next LIVE slice to rebuild the 80-byte header (subscribe reconnect, en1). */
void mine_live_reset(void);

#endif /* __MINE_H__ */
