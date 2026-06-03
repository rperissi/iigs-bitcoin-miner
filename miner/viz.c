/*
 * viz.c - GS Miner LIVE dashboard (Stage 1: real local SHA-256d mining).
 *
 * Loads the contract frame plate (PANEL = frame.shr), installs it, draws the
 * static chrome ONCE, then runs the verified SHA-256d nonce loop on the embedded
 * demo job (the same one minecore.c proves byte-for-byte against mock_pool.py).
 * Every readout is fed from REAL measurements: hashrate (hashes / GetTick),
 * total hashes, uptime, current nonce, JOBS (live work units the pool has pushed
 * this session), "shares" (nonces meeting the easy 1-zero-byte target). Best
 * leading-zero bits lives in the bottom-left scope. The scope flows from real hash output
 * bytes, the VU is a rolling 60s hashrate history, and ODDS / ETA are computed
 * live from the network difficulty (-> the "trillions of years" joke calculates
 * itself). LIVE also decodes NET DIFF (from the job's nbits) and BLOCK height
 * (BIP34, from the coinbase), and the same real difficulty feeds ODDS / ETA.
 *
 * SHR 320 mode: $E12000 pixels (4bpp, 2px/byte), $E19D00 SCBs, $E19E00 palettes.
 * Mirrors viz/render_panel.py coords; colours via miner/contract.h.
 *
 * Build (LARGE memory model: -b, so combined code spans >1 bank. The SHA library was
 * patched to be -b-safe in GSMINE76 - see mine.c / 65816-crypto/sha256.asm):
 *   occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c paths.c -L. -llib65816hash -o viz
 *   iix chtyp -t s16 viz
 * Keys : SPACE = run/stop,  ESC/Q = quit.
 * Mouse: self-drawn arrow cursor (no QuickDraw); click RUN / STOP / CONFIG.
 */
#include <stdio.h>
#include <string.h>
#include <types.h>
#include <orca.h>
#include <misctool.h>
#include <Memory.h>
#include "font_gs.h"
#include "font_w_gs.h"
#include "logo_gs.h"
#include "contract.h"
#include "contract_cfg.h"
#include "sha256.h"
#include "tcpip.h"
#include "stratum.h"
#include "mine.h"
#include "numfmt.h"
#include "cfg.h"
#include "mlog.h"
#include "paths.h"

#define PIXDST ((unsigned char *)0x00E12000L)
#define SCBDST ((unsigned char *)0x00E19D00L)
#define PALDST ((unsigned char *)0x00E19E00L)
#define NEWVIDEO ((unsigned char *)0x00C029L)
/* Border color register ($C034): low nibble = 16-color border index (0 = black);
 * high nibble is the RTC/clock interface - preserve it. We force the border black
 * (old-app trick) so the screen framing matches the dashboard's black recesses. */
#define BORDERREG ((unsigned char *)0x00C034L)
#define KBD    ((unsigned char *)0x00C000L)
#define STROBE ((unsigned char *)0x00C010L)

#define SHR_SAVE   0x8000L
#define PIX_BYTES  32000L
#define SCB_BYTES  200L
#define PAL_BYTES  512L
#define BLOB_BYTES (PIX_BYTES + SCB_BYTES + PAL_BYTES)
#define TICKS_PER_SEC 60UL
/* PANEL / CONFIG plates live in <appdir>/SYSFILES/ - resolved at runtime via
 * miner_sysfile() (see paths.c) so the app works from any folder, not just a volume
 * literally named MINERAPPS. */

/* ---- CONFIG page geometry (mirrors viz/build_config.py) ---- */
#define CF_LBL_X      10
#define CF_VAL_X      70
#define CF_ROW0       38            /* WORKER */
#define CF_ROW1       54            /* WALLET */
#define CF_ROW2       70            /* POOL */
#define CF_ROW3       86            /* BACKUP */
#define CF_ROW_H      13
#define CF_VAL_W      238           /* MODE / WORKER / WALLET full value well */
#define CF_PORT_LBL_X 214
#define CF_PORT_VAL_X 244
#define CF_PORT_VAL_W 64
#define CF_IP_W       140           /* POOL / BACKUP host well (PORT_LBL_X-VAL_X-4) */
#define CF_BTN_H      19
#define CF_SAVE_X     210
#define CF_SAVE_Y     101
#define CF_DEF_X      210
#define CF_DEF_Y      124
#define CF_MODE_X     210
#define CF_MODE_Y     147
#define CF_MODE_W     48
#define CF_BTN_W      100
#define CF_CAN_X      210
#define CF_CAN_Y      170
#define CF_CAN_W      48
#define CF_QUIT_X     262
#define CF_QUIT_Y     170
#define CF_QUIT_W     48
#define CF_GREY       5

#define CADV (GLYPH_W + 1)
#define CONFIG_GREY 3
#define CF_HIT_LIVE   104           /* LIVE mode button */
#define CF_HIT_DEMO   105           /* DEMO mode button */
#define CF_LBL_Y(y)   ((y) + (CF_ROW_H - GLYPH_H) / 2)

#define APPVER "V0.95"

/* config corner screws (drawn after buttons so QUIT never covers them) */
#define CFG_SCREW_TOP_Y  30
#define CFG_SCREW_BTM_Y  191
#define CFG_SCREW_LX     9
#define CFG_SCREW_RX     310

/* text insets (mirror viz/layout.py) */
#define FLD_LX  104
#define FLD_LW  42
#define FLD_VX  152
#define FLD_VW  91
#define FLD_LY0 11
#define FLD_VY0 11
#define FLD_DY  12

#define WAL_LX  15
#define WAL_LW  36
#define WAL_VX  59
#define WAL_VW  242
#define WAL_LY  54
#define WAL_VY  54

#define RO_LX   12
#define RO_RX   246
#define RO_VW   62
#define RO_DY   21
#define RO_VY0  70
#define RO_LY0  80
#define RO_LW   62

#define HASH_LX 11
#define HASH_LW 44
#define HASH_VX 63
#define HASH_VW 170
#define HASH_LY 158
#define HASH_VY 157

#define ODDS_LX 13           /* aligned with BEST below, centered on the plate */
#define ODDS_LW 22
#define ODDS_VX 41
#define ODDS_VW 39           /* value well trimmed 42->39 (8-char odds still fits) */
#define ODDS_LY 175
#define ODDS_VY 174

#define ETA_LX  13           /* BEST label: past the screw (cx=9), aligned under ODDS */
#define ETA_LW  22
#define ETA_VX  41
#define ETA_VW  39
#define ETA_LY  186
#define ETA_VY  185

#define TICKER_X 87          /* well slid 3px left (90->87) for the spelled-out ETA gag */
#define TICKER_W 145
#define TICKER_Y 179

#define BTN_RUN_X  253
#define BTN_RUN_Y  9
#define BTN_RUN_W  24
#define BTN_RUN_H  13
#define BTN_STOP_X 279
#define BTN_STOP_Y 9
#define BTN_STOP_W 24
#define BTN_STOP_H 13
#define BTN_CFG_X  253
#define BTN_CFG_Y  25
#define BTN_CFG_W  50
#define BTN_CFG_H  13

#define TCP_LX  256
#define TCP_LW  30
#define TCP_LY  40
#define TCP_LAMP_X 291
#define TCP_LAMP_Y 39
#define TCP_LAMP_D 6

#define SCOPE_AXIS_LX 85
#define SCOPE_FF_Y    73
#define SCOPE_00_Y    133
#define SCOPE_CAP_X   94
#define SCOPE_CAP_Y   143
#define SCOPE_CAP_W   138
/* SW (scope width) + BATCH now live with the mining core in mine.h / mine.c */
#define REDRAW_TICKS 30UL      /* ~0.5s numbers refresh (cheap) */
#define SCOPE_TICKS 90UL       /* ~1.5s hash scope refresh (heavy) */
#define STAT_TICKS 900UL       /* ~15s on-disk STAT heartbeat (diag log) */
#define MOVE_COAST 45UL        /* ~0.75s: keep hashing paused this long after the last pointer */
                               /* move, so continuous aiming never gets a 130ms hash freeze */
/* We ClampMouse directly to 320x200, so ReadMouse hands back screen pixels and
 * no scaling is needed. (Kept as a knob: set to 1 if a build clamps to 640.) */
#define MOUSE_XSHIFT 0
#define MOUSE_TRANSP 0x01      /* SetMouse transparent mode: position tracks w/o Event Mgr */
#define CUR_W 6                /* self-drawn arrow cursor (no QuickDraw needed) */
#define CUR_H 9
#define VU_X    243            /* hashrate graph well (geometry) */
#define VU_Y    163
#define VU_WW   68
#define VU_WH   25
#define VU_IH   (VU_WH - 2)
#define VU_IW   (VU_WW - 2)
#define GR_W    VU_IW          /* graph columns = inner width (66) */
#define GR_H    VU_IH          /* graph rows = inner height (23) */
#define GRAPH_TICKS 54UL       /* ~0.9s/column -> 66 cols ~= 60s window */
#define VU_5S_X   246
#define VU_AXIS_Y 188
#define VU_60S_RX 311
#define SECS_PER_YEAR 31557600.0

/* The demo job (COINBASE_HEX / VERSION / NTIME / NBITS) + the LIVE extranonce2 now
 * live with the mining core in mine.c. */

/* ---- placeholder network facts until the pool is wired (Stage 2) ---- */
#define NET_DIFF   8.81e13     /* shown as "88.1 T" */
#define BLOCK_H    842317UL

/* ---- operator config (editable on the CONFIG page) ----
 * Ship defaults are the two real solo pools we vetted: PRIMARY solo.ckpool.org
 * (Con Kolivas' long-running, multi-node solo pool - rock-solid uptime) with
 * FAILOVER to public-pool.io (open-source, nice dashboard, but single-node/flakier).
 * For dev, point POOL at the Mac mock (mock_pool.py, 192.168.2.1:3333) by hand.
 * These are RAM-mutable; Stage 3b persists them to /MINERAPPS/SYSFILES/MINER.CONF. */
/* non-static: shared with cfg.c (the CONFIG subsystem, a dynamic segment) via cfg.h */
char cfg_mode[8]    = "DEMO";
char cfg_worker[20] = "GSMINER";
char cfg_wallet[64] = "3CfSNGtkdpGMyKWx57Vr93MdHyP2UQgKao";
char cfg_pool[32]   = "SOLO.CKPOOL.ORG";
char cfg_pport[8]   = "3333";
char cfg_back[32]   = "PUBLIC-POOL.IO";
char cfg_bport[8]   = "3333";

/* first-run defaults (RESTORE DEFAULTS button reverts every field to these) */
static const char * const CF_DEF[CF_NF] = {
    "DEMO", "GSMINER", "3CfSNGtkdpGMyKWx57Vr93MdHyP2UQgKao", "SOLO.CKPOOL.ORG", "3333",
    "PUBLIC-POOL.IO", "3333"
};

/* editable-field model: buffer, max chars, and the LCD-well rectangle */
char * const cf_buf[CF_NF] =
    { cfg_mode, cfg_worker, cfg_wallet, cfg_pool, cfg_pport, cfg_back, cfg_bport };
const int cf_max[CF_NF] = { 4, 16, 62, 28, 5, 28, 5 };
static const int cf_wx[CF_NF]  =
    { CF_VAL_X, CF_VAL_X, CF_VAL_X, CF_VAL_X, CF_PORT_VAL_X, CF_VAL_X, CF_PORT_VAL_X };
static const int cf_wy[CF_NF]  =
    { 0, CF_ROW0, CF_ROW1, CF_ROW2, CF_ROW2, CF_ROW3, CF_ROW3 };
static const int cf_ww[CF_NF]  =
    { CF_VAL_W, CF_VAL_W, CF_VAL_W, CF_IP_W, CF_PORT_VAL_W, CF_IP_W, CF_PORT_VAL_W };
static int g_open_cfg;                   /* set by the CONFIG button; serviced in main loop */
static int g_cfg_badf = -1;              /* field index flagged invalid (drawn red), or -1 */
int g_demo = 1;                          /* 1 = local SHA-256 demo (no pool); 0 = live Stratum */

static unsigned char *g_save, *g_blob;
static unsigned char g_videomode;          /* $C029 at launch, restored on exit */
static unsigned char g_border;             /* $C034 at launch, restored on exit */
/* mining state shared with mine.c (declared extern in mine.h, defined here) */
struct sha256_context *g_ctx;
unsigned char g_header[80];
unsigned char g_midstate[32];   /* M5: cached inner-hash state after g_header[0..63] */
char g_hexcat[1152];              /* coinbase hex scratch (LIVE header build): holds
                                     coinb1+en1+en2+coinb2 for a real-pool job */
unsigned long g_live_gen = 0;     /* pool job gen the LIVE header was built from */

/* live stats */
unsigned long g_hashes, g_shares, g_nonce;
int g_bestbits;
int g_running = 1;                      /* non-static: shared with cfg.c via cfg.h */
int g_tcp = TCP_OFF;                    /* lamp value; in LIVE driven by strat state */
int g_strat_last = STR_OFF;             /* last Stratum state we drew (edge detect) */

/* Live NET DIFF / BLOCK / ODDS decode cache. diff_from_nbits is a ~48-step SANE
 * double loop and height_from_coinb1 strlen-scans the coinbase; ODDS runs fmt_exp.
 * Re-running these every 0.5s redraw is what halved the hashrate (draw-bound loop).
 * Decode once per job gen, repaint these wells only when dirty (value or state
 * changed, or a full panel repaint requested one). */
static unsigned long g_dec_gen    = 0;          /* job gen the cache was built from */
static double        g_dec_diff   = NET_DIFF;   /* cached network difficulty */
static unsigned long g_dec_height = BLOCK_H;    /* cached block height */
static int           g_netblk_dirty = 1;        /* repaint NET DIFF + BLOCK + ODDS */
static int           g_odds_run     = -1;       /* running-state ODDS was last drawn for */
static LongWord g_start;
static LongWord g_run_start;             /* tick the CURRENT mining session began */
static unsigned long g_run_frozen;       /* uptime (s) frozen at the moment of STOP */
unsigned char g_last[32];                 /* most recent digest (for hash line/scope) */

/* hash scope ring + 60s hashrate graph history */
unsigned char g_ring[SW];
int g_rhead;
static unsigned long g_graph[GR_W];     /* SMOOTHED rate per column (milli-H/s); [GR_W-1]=now */
static unsigned long g_gsm;             /* EMA of the raw rate -> clean trace, no per-sample noise */
static unsigned long g_gavg;            /* slow centre line for the AC-coupled display */

/* shared off-screen back-buffer (scope, graph, and flicker-free text blits) */
static int g_bh[SW];
static unsigned char g_pixbuf[70 * (SW / 2)];

/* region state for flicker-free text: render to g_pixbuf, then blit once */
static int g_drawbuf, g_rx0, g_ry0, g_rw, g_rnbc, g_rh;

/* self-drawn mouse cursor (hotspot = top-left). 0=transparent 1=black 2=white */
static int g_cx = 160, g_cy = 100, g_cshown, g_pbtn;
static int g_cur_dirty;                  /* a repaint lifted the pointer; restamp after the frame */
static unsigned char g_cunder[CUR_W * CUR_H];
static void cursor_hide(void);           /* fwd: used by the blit primitives below */
static void cursor_show(void);
static int  cursor_overlaps(int x, int y, int w, int h);
static void cursor_clear_for(int x, int y, int w, int h);
static const unsigned char CUR_BMP[CUR_H][CUR_W] = {
    {1, 0, 0, 0, 0, 0},
    {1, 1, 0, 0, 0, 0},
    {1, 2, 1, 0, 0, 0},
    {1, 2, 2, 1, 0, 0},
    {1, 2, 2, 2, 1, 0},
    {1, 2, 2, 2, 2, 1},
    {1, 2, 2, 1, 1, 1},
    {1, 1, 2, 2, 1, 0},
    {0, 0, 1, 1, 0, 0}
};

/* ============================ SHR drawing ============================ */
static void setpix(int x, int y, unsigned char c)
{
    unsigned char *p;
    if (g_drawbuf) {                          /* render into the off-screen region */
        int lx = x - g_rx0, ly = y - g_ry0;
        if (lx < 0 || lx >= g_rw || ly < 0 || ly >= g_rh) return;
        p = g_pixbuf + ly * g_rnbc + (lx >> 1);
        if (lx & 1) *p = (unsigned char)((*p & 0xF0) | (c & 0x0F));
        else        *p = (unsigned char)((*p & 0x0F) | ((c & 0x0F) << 4));
        return;
    }
    if (x < 0 || x > 319 || y < 0 || y > 199) return;
    p = PIXDST + (long)y * 160L + (x >> 1);
    if (x & 1) *p = (unsigned char)((*p & 0xF0) | (c & 0x0F));
    else       *p = (unsigned char)((*p & 0x0F) | ((c & 0x0F) << 4));
}

static void fillrect(int x, int y, int w, int h, unsigned char c)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            setpix(x + i, y + j, c);
}

/* fast black clear: writes whole 0x00 bytes; nibble RMW only at odd edges */
static void clearblk(int x, int y, int w, int h)
{
    int j, i, xe = x + w;
    unsigned char *row;
    if (x < 0) x = 0;
    if (xe > 320) xe = 320;
    for (j = 0; j < h; j++) {
        int yy = y + j;
        if (yy < 0 || yy > 199) continue;
        row = PIXDST + (long)yy * 160L;
        i = x;
        if (i & 1) { row[i >> 1] &= 0xF0; i++; }          /* clear right nibble */
        while (i + 1 < xe) { row[i >> 1] = 0x00; i += 2; } /* whole bytes */
        if (i < xe) { row[i >> 1] &= 0x0F; }               /* clear left nibble */
    }
}

static void ellipse(int x, int y, int w, int h, unsigned char c)
{
    int i, j;
    long rx = w, ry = h;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            long dx = (long)(2 * i - (w - 1)) * ry;
            long dy = (long)(2 * j - (h - 1)) * rx;
            if (dx * dx + dy * dy <= rx * ry * rx * ry)
                setpix(x + i, y + j, c);
        }
}

static int drawchar(int x, int y, char ch, unsigned char c)
{
    int gy, gx, idx;
    unsigned char bits;
    static const unsigned char GLYPH_S[GLYPH_H] = {  /* small-caps 's': 3px wide, 5 tall */
        0x00, 0x0E, 0x08, 0x0E, 0x02, 0x0E
    };
    static const unsigned char GLYPH_V[GLYPH_H] = {  /* lowercase 'v': dropped to baseline (row5) to sit in line with digits */
        0x00, 0x00, 0x00, 0x09, 0x09, 0x06
    };
    if (ch == 's' || ch == 'v') {
        const unsigned char *gp = (ch == 's') ? GLYPH_S : GLYPH_V;
        for (gy = 0; gy < GLYPH_H; gy++) {
            bits = gp[gy];
            for (gx = 0; gx < GLYPH_W; gx++)
                if (bits & (1 << (GLYPH_W - 1 - gx))) setpix(x + gx, y + gy, c);
        }
        return x + CADV;
    }
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch < FONT_LO || ch > FONT_HI) ch = ' ';
    idx = (ch - FONT_LO) * GLYPH_H;
    for (gy = 0; gy < GLYPH_H; gy++) {
        bits = FONT[idx + gy];
        for (gx = 0; gx < GLYPH_W; gx++)
            if (bits & (1 << (GLYPH_W - 1 - gx))) setpix(x + gx, y + gy, c);
    }
    return x + CADV;
}

static int text(int x, int y, const char *s, unsigned char c)
{
    while (*s) x = drawchar(x, y, *s++, c);
    return x;
}

static void text_clip(int x, int y, const char *s, unsigned char c, int maxw)
{
    int x0 = x;
    while (*s) {
        if (x - x0 + GLYPH_W > maxw) break;
        x = drawchar(x, y, *s++, c);
    }
}

/* ---- 5x8 case-preserving font for the WALLET field (legible mixed case) ---- */
static void drawchar_w(int x, int y, char ch, unsigned char c)
{
    int gy, gx, idx;
    unsigned char bits;
    if (ch < FW_LO || ch > FW_HI) ch = ' ';
    idx = (ch - FW_LO) * FW_H;
    for (gy = 0; gy < FW_H; gy++) {
        bits = FONTW[idx + gy];
        for (gx = 0; gx < FW_W; gx++)
            if (bits & (1 << (FW_W - 1 - gx))) setpix(x + gx, y + gy, c);
    }
}

/* Draw a wallet into a well of width maxw. Normal spacing for short addresses;
 * auto-condense to tight spacing so any legacy/bech32 (<=47 chars) fits without
 * truncation. Over-length (taproot 62) clips gracefully at the edge. Returns end x. */
static int wallet_text(int x, int y, const char *s, unsigned char c, int maxw)
{
    int n = 0, adv, cx = x;
    while (s[n]) n++;
    adv = FW_W + 1;                              /* normal: 6px advance */
    if (n && n * adv - 1 > maxw) adv = FW_W;     /* condense: 5px advance */
    while (*s) {
        if (cx - x + FW_W > maxw) break;         /* clip at well edge */
        drawchar_w(cx, y, *s++, c);
        cx += adv;
    }
    return cx;
}

/* ---- flicker-free text: clear a byte-aligned region in g_pixbuf, draw into it,
 * then blit the whole strip to video in one pass (video never shows a blank). */
static void region_begin(int x, int y, int w, int h)
{
    int x0 = x & ~1, x1 = (x + w + 1) & ~1, r, bc;
    g_rx0 = x0; g_ry0 = y; g_rw = x1 - x0; g_rnbc = (x1 - x0) >> 1; g_rh = h;
    for (r = 0; r < h; r++) {
        unsigned char *s = g_pixbuf + r * g_rnbc;
        for (bc = 0; bc < g_rnbc; bc++) s[bc] = 0;
    }
}

/* like region_begin, but pre-fill the strip with a solid colour nibble (used for
 * the CONFIG page's blue LCD wells so edited text blits flicker-free over blue). */
static void region_fill(int x, int y, int w, int h, unsigned char nib)
{
    int x0 = x & ~1, x1 = (x + w + 1) & ~1, r, bc;
    unsigned char b = (unsigned char)((nib << 4) | (nib & 0x0F));
    g_rx0 = x0; g_ry0 = y; g_rw = x1 - x0; g_rnbc = (x1 - x0) >> 1; g_rh = h;
    for (r = 0; r < h; r++) {
        unsigned char *s = g_pixbuf + r * g_rnbc;
        for (bc = 0; bc < g_rnbc; bc++) s[bc] = b;
    }
}

static void region_blit(void)
{
    int r, xbyte = g_rx0 >> 1;
    cursor_clear_for(g_rx0, g_ry0, g_rw, g_rh);   /* don't paint over the pointer */
    for (r = 0; r < g_rh; r++)
        memcpy(PIXDST + (long)(g_ry0 + r) * 160L + xbyte,
               g_pixbuf + r * g_rnbc, (size_t)g_rnbc);
}

static void upd_text(int x, int y, int w, const char *s, unsigned char c)
{
    region_begin(x - 1, y - 1, w + 2, GLYPH_H + 2);
    g_drawbuf = 1;
    text_clip(x, y, s, c, w);
    g_drawbuf = 0;
    region_blit();
}

static int textw(const char *s) { int n = 0; while (s[n]) n++; return n ? n * CADV - 1 : 0; }
static void text_r(int xr, int y, const char *s, unsigned char c) { text(xr - textw(s), y, s, c); }
static void text_c(int x, int w, int y, const char *s, unsigned char c) { text(x + (w - textw(s)) / 2, y, s, c); }

/* ============================ number formatting ============================ */
/* fmt_compact / fmt_exp / fmt_years / fmt_years_words / fmt_uptime moved to numfmt.c
 * for modularity (project built with the large memory model). See numfmt.h. */

/* ============================ SHA-256d mining ============================ */
/* The mining core (header build, M5 midstate fast path, the nonce-grinding slice)
 * moved to mine.c for modularity (project built with the large memory model so the
 * combined code can span banks). build_job() + mine_slice() are declared in mine.h;
 * the shared g_* mining state is defined above and used across the split. */

/* ============================ widgets ============================ */
static void coin(int x, int y, int w, int h)
{
    int cx = x + w / 2;
    ellipse(x, y, w, h, H_GOLD);
    /* Bitcoin stems: short black ticks poking out the top + bottom centre */
    setpix(cx - 1, y, C_BLACK);         setpix(cx, y, C_BLACK);
    setpix(cx - 1, y + h - 1, C_BLACK); setpix(cx, y + h - 1, C_BLACK);
    drawchar(x + w / 2 - 2, y + h / 2 - 3, 'B', C_BLACK);
}

/* chiseled button cap; down = pressed (inverted bevel + nudged label) */
static void button(int x, int y, int w, int h, unsigned char fill,
                   const char *label, unsigned char lc, int down)
{
    unsigned char hi = down ? C_BLACK : C_WHITE, lo = down ? C_WHITE : C_BLACK;
    int o = down ? 1 : 0;
    fillrect(x, y, w, h, fill);
    fillrect(x, y, w, 1, hi);  fillrect(x, y, 1, h, hi);
    fillrect(x, y + h - 1, w, 1, lo); fillrect(x + w - 1, y, 1, h, lo);
    fillrect(x + 1, y + 1, w - 2, 1, hi); fillrect(x + 1, y + 1, 1, h - 2, hi);
    fillrect(x + 1, y + h - 2, w - 2, 1, lo); fillrect(x + w - 2, y + 1, 1, h - 2, lo);
    text_c(x + o, w, y + (h - GLYPH_H) / 2 + o, label, lc);
}

/* tactile press: draw a button pressed, hold a beat, release - so a click visibly
 * "clunks" before its action runs. Caller has already hidden the cursor. */
static void press_flash(int x, int y, int w, int h, unsigned char fill,
                        const char *label, unsigned char lc)
{
    LongWord t;
    button(x, y, w, h, fill, label, lc, 1);
    for (t = GetTick(); GetTick() - t < 8UL; ) ;   /* ~130ms */
    button(x, y, w, h, fill, label, lc, 0);
}

/* TCP/IP status LED (well at 291,39); Stage 2 wires Marinetti poll + flash */
static void tcp_lamp(int lx, int ly, int d)
{
    unsigned char c;
    int flash = (int)((GetTick() / 15UL) & 1UL);
    if (g_tcp == TCP_OFF)       c = CONFIG_GREY;
    else if (g_tcp == TCP_DOWN) c = H_RED;
    else if (g_tcp == TCP_ACT && flash) c = H_GREEN_DK;   /* pulse light<->dark green */
    else                        c = H_GREEN;
    ellipse(lx, ly, d, d, c);
    if (g_tcp >= TCP_UP && !(g_tcp == TCP_ACT && flash))
        setpix(lx + 1, ly + 1, C_WHITE);
}

static void draw_tcp(void)
{
    text_clip(TCP_LX, TCP_LY, "TCP/IP", C_BLACK, TCP_LW);
    tcp_lamp(TCP_LAMP_X + 1, TCP_LAMP_Y + 1, TCP_LAMP_D);
}

/* restore the badge slot's metal from the loaded frame blob (so variable-length
 * badge text never leaves stale glyphs behind). Bytes 15..47 = x 30..95, clear of
 * the code-drawn coin (well x 14..28); rows 40..47 cover the 6px glyph height. */
static void badge_restore(void)
{
    int j, b0 = 30 >> 1, b1 = 96 >> 1;
    for (j = 40; j < 48; j++) {
        long off = (long)j * 160L;
        memcpy(PIXDST + off + b0, g_blob + off + b0, (size_t)(b1 - b0));
    }
}

/* centre a badge string in the groove (x 30..96) so every state reads balanced */
static void badge_text(const char *s, unsigned char c) { text_c(30, 66, 41, s, c); }

/* coin-badge slot: DEMO MODE (gold), live states from the Stratum client. */
static void draw_mode_badge(void)
{
    badge_restore();
    if (g_demo) { badge_text("DEMO MODE", H_GOLD); return; }
    switch (strat_state()) {
    case STR_MINING:    badge_text("SHA-256D",  H_CYAN); break;  /* job in hand: mining pool work */
    case STR_CONNECTED:
    case STR_SUBSCRIBED:badge_text("SUBSCRIBE", H_GOLD); break;  /* TCP up, Stratum handshake */
    case STR_RESOLVING: badge_text("RESOLVE",   H_GOLD); break;
    case STR_OPENING:   badge_text("CONNECT",   H_GOLD); break;
    case STR_RETRY:     badge_text("RETRY",        H_RED);  break;
    case STR_NO_IP:     badge_text("NO LINK",      H_RED);  break;
    case STR_NO_TCP:    badge_text("NO MARINETTI", H_RED);  break;
    case STR_CONN_FAIL: badge_text("POOL FAIL",    H_RED);  break;  /* link up, pool won't connect */
    case STR_DNS_FAIL:  badge_text("DNS FAIL",     H_RED);  break;  /* pool name won't resolve */
    case STR_POOL_FAIL: badge_text("POOL ERR",     H_RED);  break;  /* connected but pool misbehaves */
    default:            badge_text("NO IP",     H_RED);  break;
    }
}

/* green LED value in the black screen; flicker-free off-screen blit (4x6 font) */
static void readout(int x, int cy, const char *val)
{
    upd_text(x, cy, RO_VW, val, M_LED);
}

/* active != 0 paints the row green (matches the live TCP lamp) so the pool
 * currently carrying work stands out from its idle sibling at a glance. */
static void field(int i, const char *label, const char *val, int active)
{
    int yl = FLD_LY0 + i * FLD_DY, yv = FLD_VY0 + i * FLD_DY;
    text_clip(FLD_LX, yl, label, active ? H_GREEN_DK : C_BLACK, FLD_LW);
    text_clip(FLD_VX, yv, val, active ? H_GREEN : H_CYAN, FLD_VW);
}

/* which configured pool is actually mining right now: 1 = POOL (primary),
 * 2 = FAILOVER (backup), -1 = none (demo, or live but not yet mining). */
static int active_pool_idx(void)
{
    if (g_demo || strat_state() != STR_MINING) return -1;
    return strat_on_backup() ? 2 : 1;
}

/* (re)draw the POOL / FAILOVER rows with the live one highlighted. Same text,
 * so a colour flip just recolours the existing glyphs in place. */
static void draw_pool_fields(void)
{
    int a = active_pool_idx();
    field(1, "POOL",     cfg_pool, a == 1);
    field(2, "FAILOVER", cfg_back, a == 2);
}

/* double-buffered scope: build the whole rectangle in a RAM back-buffer (the
 * slow per-pixel work is now INVISIBLE), then blit each row to video in one
 * tight byte copy -> the on-screen update is a fast block move, not a slow
 * top-down "sweep". Requires even x and even w (94/138 qualify). */
static void scope(int x, int y, int w, int h)
{
    int col, r, bc, nbc = w >> 1, rci;
    unsigned char *dst, *src, cl, cr;
    int xbyte = x >> 1;
    for (col = 0; col < w; col++)
        g_bh[col] = 2 + (int)((long)g_ring[(g_rhead + col) % SW] * (h - 3) / 255L);
    /* render into back-buffer: r = 0 is the TOP row */
    for (r = 0; r < h; r++) {
        int rowfb = h - 1 - r;                   /* distance from bottom */
        rci = M_RB0 + rowfb * M_RB_N / h;
        if (rci >= M_RB0 + M_RB_N) rci = M_RB0 + M_RB_N - 1;
        src = g_pixbuf + r * nbc;
        for (bc = 0; bc < nbc; bc++) {
            cl = (rowfb < g_bh[bc * 2])     ? (unsigned char)rci : C_BLACK;
            cr = (rowfb < g_bh[bc * 2 + 1]) ? (unsigned char)rci : C_BLACK;
            src[bc] = (unsigned char)((cl << 4) | cr);
        }
    }
    /* blit back-buffer to video (fast sequential byte copy per row) */
    for (r = 0; r < h; r++) {
        dst = PIXDST + (long)(y + r) * 160L + xbyte;
        src = g_pixbuf + r * nbc;
        memcpy(dst, src, (size_t)nbc);
    }
}

static void draw_scope(void)
{
    cursor_clear_for(94, 71, 138, 70);
    if (g_running) scope(94, 71, 138, 70);      /* live hash-output spectrum */
    else           clearblk(94, 71, 138, 70);   /* stopped: dark, idle scope */
}

/* ---- scrolling 60s hashrate line graph (replaces the VU bars) ----
 * Each column holds one hashrate sample; the newest is on the right. Every
 * GRAPH_TICKS we shift left and append, so the trace scrolls through time.
 * Rendered into the shared back-buffer then blitted in one pass (no sweep). */
static void graph_push(unsigned long rate)
{
    int i;
    /* g_gsm: light EMA so the sub-second 16<->20 jitter becomes a CLEAN trace
     *        while the real multi-second movement is preserved.
     * g_gavg: slow centre so the trace rides the green mid (AC-coupled display). */
    if (g_gsm == 0)  g_gsm = rate;
    else             g_gsm = (g_gsm * 3UL + rate) / 4UL;   /* ~2s time const: kills the */
                                                           /* +/-1 hash quantization noise */
    if (g_gavg == 0) g_gavg = g_gsm;
    else             g_gavg = (g_gavg * 15UL + g_gsm) / 16UL;
    for (i = 0; i < GR_W - 1; i++) g_graph[i] = g_graph[i + 1];
    g_graph[GR_W - 1] = g_gsm;
}

/* set one back-buffer pixel; rowfb 0 = bottom row of the well */
static void gbuf_set(int nbc, int col, int rowfb, unsigned char c)
{
    unsigned char *p;
    int r2;
    if (rowfb < 0 || rowfb >= GR_H || col < 0 || col >= GR_W) return;
    r2 = GR_H - 1 - rowfb;
    p = g_pixbuf + r2 * nbc + (col >> 1);
    if (col & 1) *p = (unsigned char)((*p & 0xF0) | (c & 0x0F));
    else         *p = (unsigned char)((*p & 0x0F) | ((c & 0x0F) << 4));
}

static void draw_graph(void)
{
    int nbc = GR_W >> 1, xbyte = (VU_X + 1) >> 1;
    int gy[GR_W];
    unsigned char gc[GR_W];
    int c, r, bc, yy, y0, y1, lo, hi, f, h, mid;
    long dv, off;
    unsigned long scale;
    unsigned char *dst, *s;

    /* AC-coupled with a MODERATE gain: centre on the running average; a ~25%
     * deviation spans the full height. The smoothed trace typically wanders only
     * a few percent, so it rides the green mid and rises/dips a few pixels with
     * the real hashrate -- a clean line, never clamped to the top. */
    cursor_clear_for(VU_X + 1, VU_Y + 1, GR_W, GR_H);

    if (!g_running) {                      /* stopped: pin a flat line to the zero baseline */
        for (r = 0; r < GR_H; r++) { s = g_pixbuf + r * nbc; for (bc = 0; bc < nbc; bc++) s[bc] = 0; }
        for (c = 0; c < GR_W; c++) gbuf_set(nbc, c, 0, B_VUG);    /* green line on the bottom row */
        for (r = 0; r < GR_H; r++) {
            dst = PIXDST + (long)(VU_Y + 1 + r) * 160L + xbyte;
            memcpy(dst, g_pixbuf + r * nbc, (size_t)nbc);
        }
        return;
    }

    mid = (GR_H - 1) / 2;
    scale = g_gavg / 2UL + 1UL;            /* lower gain: a ~50% swing spans the well, so */
                                           /* the smoothed line wanders gently, not jaggedly */

    for (c = 0; c < GR_W; c++) {
        dv = (long)g_graph[c] - (long)g_gavg;
        off = dv * (long)mid / (long)scale;
        h = mid + (int)off;
        if (h < 0) h = 0;
        if (h > GR_H - 1) h = GR_H - 1;
        gy[c] = h;
        f = h * 100 / (GR_H - 1);
        gc[c] = (f < 50) ? B_VUG : (f < 78 ? B_VUY : B_VUR);
    }

    for (r = 0; r < GR_H; r++) {                 /* clear back-buffer to black */
        s = g_pixbuf + r * nbc;
        for (bc = 0; bc < nbc; bc++) s[bc] = 0;
    }

    /* CLEAN 1px line: connect each point to the previous one, but CAP the vertical
     * run at 3px so a rare jump can never paint a tall bar -> stays a line, never
     * a filled noise block. */
    for (c = 0; c < GR_W; c++) {
        y0 = gy[c];
        y1 = (c > 0) ? gy[c - 1] : gy[c];
        lo = (y0 < y1) ? y0 : y1;
        hi = (y0 > y1) ? y0 : y1;
        if (hi - lo > 3) {                       /* clamp the connector length */
            if (y0 >= y1) lo = hi - 3; else hi = lo + 3;
        }
        for (yy = lo; yy <= hi; yy++) gbuf_set(nbc, c, yy, gc[c]);
    }

    gbuf_set(nbc, GR_W - 1, gy[GR_W - 1], C_WHITE);      /* live cursor dot */

    for (r = 0; r < GR_H; r++) {                 /* blit (fast block move per row) */
        dst = PIXDST + (long)(VU_Y + 1 + r) * 160L + xbyte;
        memcpy(dst, g_pixbuf + r * nbc, (size_t)nbc);
    }
}

/* ============================ static + dynamic draw ============================ */
static void draw_state(void)
{
    button(BTN_RUN_X, BTN_RUN_Y, BTN_RUN_W, BTN_RUN_H, H_GREEN, "RUN",
           C_BLACK, g_running);
    button(BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H, H_RED, "STOP",
           C_WHITE, !g_running);
    button(BTN_CFG_X, BTN_CFG_Y, BTN_CFG_W, BTN_CFG_H, CONFIG_GREY, "CONFIG",
           C_WHITE, 0);
    draw_tcp();
}

/* Blit the locked gold-GS / red-MINER wordmark (miner/logo_gs.h, generated by
 * viz/logo_depth_proof.py) into the logo well. 255 = transparent (the panel recess
 * shows through). The version is NOT baked in — draw_static stamps the dynamic
 * APPVER on top so a rev bump updates it without regenerating the art. */
static void draw_logo(void)
{
    const unsigned char *p = LOGO_GS;
    int x, y;
    unsigned char c;
    for (y = 0; y < LOGO_GS_H_PX; y++)
        for (x = 0; x < LOGO_GS_W; x++) {
            c = *p++;
            if (c != LOGO_GS_NONE) setpix(LOGO_GS_X + x, LOGO_GS_Y + y, c);
        }
}

static void draw_static(void)
{
    draw_logo();
    {                                        /* dynamic version stamp (grey): lowercase 'v' for a lighter look */
        static char vbuf[12];
        int vi;
        for (vi = 0; APPVER[vi] && vi < 11; vi++) vbuf[vi] = APPVER[vi];
        vbuf[vi] = '\0';
        if (vbuf[0] == 'V') vbuf[0] = 'v';
        text_r(92, 27, vbuf, 6);
    }
    coin(16, 40, 10, 8);
    draw_mode_badge();
    field(0, "WORKER", cfg_worker, 0);
    draw_pool_fields();
    draw_state();
    text_clip(WAL_LX, WAL_LY, "WALLET", C_BLACK, WAL_LW);
    wallet_text(WAL_VX, WAL_VY, cfg_wallet, W_CYAN, WAL_VW);   /* 5x7 sits inset in the well */
    text_clip(RO_LX, RO_LY0 + 0 * RO_DY, "HASHRATE", C_BLACK, RO_LW);
    text_clip(RO_LX, RO_LY0 + 1 * RO_DY, "SHARES",   C_BLACK, RO_LW);
    text_clip(RO_LX, RO_LY0 + 2 * RO_DY, "JOBS",     C_BLACK, RO_LW);
    text_clip(RO_LX, RO_LY0 + 3 * RO_DY, "UPTIME",   C_BLACK, RO_LW);
    text_clip(RO_RX, RO_LY0 + 0 * RO_DY, "NONCE",    C_BLACK, RO_LW);
    text_clip(RO_RX, RO_LY0 + 1 * RO_DY, "HASHES",   C_BLACK, RO_LW);
    text_clip(RO_RX, RO_LY0 + 2 * RO_DY, "NET DIFF", C_BLACK, RO_LW);
    text_clip(RO_RX, RO_LY0 + 3 * RO_DY, "BLOCK",    C_BLACK, RO_LW);
    /* NET DIFF + BLOCK + ODDS values are live (job-derived) and self-gated in
     * draw_numbers; a full repaint blanked the wells, so force one repaint. */
    g_netblk_dirty = 1;
    g_odds_run = -1;
    text(SCOPE_AXIS_LX, SCOPE_FF_Y, "FF", C_BLACK);
    text(SCOPE_AXIS_LX, SCOPE_00_Y, "00", C_BLACK);
    text_clip(SCOPE_CAP_X, SCOPE_CAP_Y, "4096 HASHES", C_BLACK, SCOPE_CAP_W);
    text_clip(HASH_LX, HASH_LY, "SHA-256", C_BLACK, HASH_LW);
    text_clip(ODDS_LX, ODDS_LY, "ODDS", C_BLACK, ODDS_LW);
    text_clip(ETA_LX, ETA_LY, "BEST", C_BLACK, ETA_LW);     /* x past the bottom-left screw */
    text_clip(245, 155, "HASHRATE 30s", C_BLACK, 66);
    text(VU_5S_X, VU_AXIS_Y, "30s", C_BLACK);
    text(VU_60S_RX - 7 - textw("0s"), VU_AXIS_Y, "0s", C_BLACK);  /* gap before screw */
}

/* parse n hex chars -> value (stops at the first non-hex digit) */
static unsigned long hexn(const char *s, int n)
{
    unsigned long v = 0;
    int i, c;
    for (i = 0; i < n; i++) {
        c = s[i];
        if      (c >= '0' && c <= '9') c -= '0';
        else if (c >= 'a' && c <= 'f') c -= ('a' - 10);
        else if (c >= 'A' && c <= 'F') c -= ('A' - 10);
        else break;
        v = (v << 4) | (unsigned long)c;
    }
    return v;
}

/* Network difficulty from the job's compact "nbits" (8 hex: 1 exponent byte +
 * 3 mantissa bytes). diff = diff1_target / target, where target = mant * 2^(8*(exp-3))
 * and diff1 = 0xFFFF * 2^208  =>  diff = (0xFFFF / mant) * 2^(232 - 8*exp). Network-wide
 * and only retargets every 2016 blocks (~2 weeks), so it's accurate but won't move
 * within a session. Falls back to the NET_DIFF constant if nbits is missing/garbage. */
static double diff_from_nbits(const char *nb)
{
    unsigned long exp, mant;
    double d;
    int k;
    if (!nb || (int)strlen(nb) < 8) return NET_DIFF;
    exp  = hexn(nb, 2);
    mant = hexn(nb + 2, 6);
    if (mant == 0) return NET_DIFF;
    d = 65535.0 / (double)mant;
    k = 232 - 8 * (int)exp;
    while (k > 0) { d *= 2.0; k--; }
    while (k < 0) { d /= 2.0; k++; }
    return d;
}

/* Block height from coinb1 via BIP34: the coinbase scriptSig opens with the height.
 * Layout in the coinbase tx prefix the pool sends us: version(4) + in-count(1) +
 * prevout hash(32) + prevout index(4) + scriptSig-len(1) = byte 42 is the height
 * PUSH length N, then N little-endian bytes. Each byte i lives at hex offset 2*i.
 * Ticks up ~every 10 min as new blocks land. 0 = couldn't parse (caller falls back). */
static unsigned long height_from_coinb1(const char *c1)
{
    int n, j;
    unsigned len;
    unsigned long h = 0;
    if (!c1) return 0;
    len = (unsigned)strlen(c1);
    if (len < 86) return 0;                     /* need at least up to the push-length byte */
    n = (int)hexn(c1 + 84, 2);                  /* byte 42: BIP34 height push length */
    if (n < 1 || n > 4) return 0;               /* sane heights are 3-4 bytes */
    if (len < (unsigned)(86 + 2 * n)) return 0;
    for (j = 0; j < n; j++)
        h |= hexn(c1 + 86 + 2 * j, 2) << (8 * j);   /* little-endian */
    return h;
}

/* cheap: live readouts, hash line, odds/eta, ticker (every ~0.5s) */
static void draw_numbers(unsigned long hps)
{
    char buf[40], tmp[24];
    unsigned long up = g_running ? (unsigned long)((GetTick() - g_run_start) / TICKS_PER_SEC)
                                 : g_run_frozen;          /* frozen while stopped */
    int i;
    double work, secs, yrs;
    /* Live network stats come from the current job; ODDS/ETA use the same real diff.
     * havejob gates the LIVE path (mining + parsed); demo/pre-job use the constant.
     *
     * CACHE the decode per job generation: diff_from_nbits is a ~48-step SANE double
     * loop and height_from_coinb1 strlen-scans the coinbase. Running both EVERY 0.5s
     * redraw (the draw path is already SANE-float-bound) collapsed the cooperative
     * loop from ~8 H/s to ~1 H/s. The pool only pushes a new job every ~30-60s, so
     * keying off job->gen runs the heavy decode a couple times a minute instead. */
    const StratJob *job = strat_job();
    int    havejob = (!g_demo && strat_state() == STR_MINING && job->gen > 0 && strat_havesub());
    double netdiff;
    if (havejob) {
        if (job->gen != g_dec_gen) {                /* new job -> decode once, then cache */
            g_dec_gen    = job->gen;
            g_dec_diff   = diff_from_nbits(job->nbits);
            g_dec_height = height_from_coinb1(job->coinb1);
            if (g_dec_height == 0) g_dec_height = BLOCK_H;   /* parse miss -> illustrative */
            g_netblk_dirty = 1;                     /* values changed -> repaint the wells */
        }
        netdiff = g_dec_diff;
    } else {
        if (g_dec_gen != 0) g_netblk_dirty = 1;     /* fell back to pre-job/demo display */
        g_dec_gen = 0;                              /* force a re-decode when a job arrives */
        netdiff = NET_DIFF;
    }

    fmt_compact(tmp, hps); sprintf(buf, "%s H/S", tmp);     readout(RO_LX, RO_VY0 + 0 * RO_DY, buf);
    if (g_demo) sprintf(buf, "%lu / 0", g_shares);                      /* accepted / rejected */
    else        sprintf(buf, "%lu / %lu", strat_accepted(), strat_rejected());
                                                        readout(RO_LX, RO_VY0 + 1 * RO_DY, buf);
    /* JOBS: live work units the pool has pushed (moves on real data even when shares
     * never tick at our hashrate). Demo has no pool, so "--" like the stopped ODDS. */
    if (g_demo) strcpy(buf, "--");
    else        sprintf(buf, "%lu", strat_jobs());
                                                        readout(RO_LX, RO_VY0 + 2 * RO_DY, buf);
    fmt_uptime(buf, up);                                readout(RO_LX, RO_VY0 + 3 * RO_DY, buf);
    sprintf(buf, "0x%08lX", g_nonce);                   readout(RO_RX, RO_VY0 + 0 * RO_DY, buf);
    fmt_compact(buf, g_hashes);                         readout(RO_RX, RO_VY0 + 1 * RO_DY, buf);
    /* NET DIFF (accurate but ~2-week-static) + BLOCK (climbs ~every 10 min) from the
     * live job. Repainted ONLY when dirty (value/state change or full repaint) so the
     * SANE fmt_si + two blits stay off the per-frame path. LIVE pre-job -> "--"; DEMO
     * shows the illustrative constants. */
    if (g_netblk_dirty) {
        if (!g_demo && !havejob) {
            readout(RO_RX, RO_VY0 + 2 * RO_DY, "--");
            readout(RO_RX, RO_VY0 + 3 * RO_DY, "--");
        } else {
            unsigned long h = havejob ? g_dec_height : BLOCK_H;   /* cached; no per-frame parse */
            fmt_si(tmp, netdiff);                       readout(RO_RX, RO_VY0 + 2 * RO_DY, tmp);
            sprintf(buf, "%lu", h);                     readout(RO_RX, RO_VY0 + 3 * RO_DY, buf);
        }
    }

    region_begin(HASH_VX - 1, HASH_VY - 1, HASH_VW + 2, GLYPH_H + 2);
    g_drawbuf = 1;
    { int x = HASH_VX;
      for (i = 31; i >= 0 && x < HASH_VX + HASH_VW - 4; i--) {
        char pair[3];
        pair[0] = "0123456789ABCDEF"[g_last[i] >> 4];
        pair[1] = "0123456789ABCDEF"[g_last[i] & 15];
        pair[2] = 0;
        x = text(x, HASH_VY, pair, B_VUG);
      } }
    g_drawbuf = 0;
    region_blit();

    /* Bottom-left pair: ODDS (block-finding odds) over BEST (best leading-zero
     * bits this session). ODDS goes -- when stopped (it's a live projection); BEST
     * is a session high-water mark, so it persists. */
    /* ODDS depends only on the (cached) difficulty + running-state, not hps, so repaint
     * it only when those change - keeps fmt_exp's SANE divides off the per-frame path. */
    if (g_netblk_dirty || g_odds_run != g_running) {
        if (!g_running) upd_text(ODDS_VX, ODDS_VY, ODDS_VW, "--", B_AMBER);
        else {
            work = netdiff * 4294967296.0;          /* expected hashes/block = diff * 2^32 */
            fmt_exp(tmp, work); sprintf(buf, "1:%s", tmp);
            upd_text(ODDS_VX, ODDS_VY, ODDS_VW, buf, B_AMBER);
        }
        g_odds_run = g_running;
    }
    g_netblk_dirty = 0;                              /* NET DIFF/BLOCK/ODDS now in sync */
    sprintf(buf, "%d BITS", g_bestbits);
    upd_text(ETA_VX, ETA_VY, ETA_VW, buf, B_AMBER);

    /* Wide well = the punchline: estimated time to mine a block, magnitudes spelled
     * out for maximum bang. Adaptive label so the longest words still fit the 142px:
     * "BLOCK ETA  1.3 TRILLION YRS" -> "ETA 1.3 QUADRILLION YRS" as needed. */
    if (!g_running || hps == 0) strcpy(buf, "BLOCK ETA  --");
    else {
        static const char *PFX[] = { "BLOCK ETA  ", "BLOCK ETA ", "ETA " };
        int p;
        work = netdiff * 4294967296.0;
        secs = work / (double)hps;
        yrs  = secs / SECS_PER_YEAR;
        fmt_years_words(tmp, yrs);
        for (p = 0; p < 3; p++) { sprintf(buf, "%s%s", PFX[p], tmp); if (textw(buf) <= TICKER_W) break; }
    }
    upd_text(TICKER_X, TICKER_Y, TICKER_W, buf, B_AMBER);
    if (g_tcp == TCP_ACT)
        draw_tcp();                         /* refresh lamp flash ~4Hz */
}

/* ============================ file + screen plumbing ============================ */
static int load_blob_path(const char *path)
{
    FILE *f; long got = 0; size_t n;
    f = fopen(path, "rb");
    if (!f) { printf("cannot open %s\n", path); return 0; }
    while (got < BLOB_BYTES) { n = fread(g_blob + got, 1, (size_t)(BLOB_BYTES - got), f); if (!n) break; got += (long)n; }
    fclose(f);
    if (got != BLOB_BYTES) { printf("short read: %ld\n", got); return 0; }
    return 1;
}
static int load_blob(void) { return load_blob_path(miner_sysfile("PANEL")); }

static void save_desktop(void)
{
    long i;
    g_videomode = *NEWVIDEO;                 /* remember Finder's video mode */
    g_border    = *BORDERREG;                /* and its border color (restored on exit) */
    for (i = 0; i < SHR_SAVE; i++) g_save[i] = PIXDST[i];
}

/* Drop SHR while the 32 KB desktop copy runs (GS/OS shows its own screen, so the
 * slow restore is invisible), then snap video mode back -> one clean step to Finder. */
static void restore_desktop(void)
{
    long i;
    *NEWVIDEO = (unsigned char)(g_videomode & 0x7F);         /* SHR off during copy */
    for (i = 0; i < SHR_SAVE; i++) PIXDST[i] = g_save[i];
    *NEWVIDEO  = g_videomode;                                /* restore pre-launch mode */
    *BORDERREG = (unsigned char)((*BORDERREG & 0xF0) | (g_border & 0x0F));  /* border back */
}

/* Paint the frame with the palette blanked to black, so the slow 32 KB pixel
 * copy is INVISIBLE (no top-to-bottom colour sweep). reveal_panel() then flashes
 * the real palette in once, so the finished dashboard appears all at once. */
static void show_panel(void)
{
    long i;
    unsigned char *pix = g_blob, *scb = g_blob + PIX_BYTES;
    *NEWVIDEO |= 0x80;                      /* SHR on (already on under GS/OS) */
    *BORDERREG &= 0xF0;                      /* force border black (low nibble 0), keep RTC bits */
    for (i = 0; i < PAL_BYTES; i++) PALDST[i] = 0;     /* blank every palette */
    for (i = 0; i < SCB_BYTES; i++) SCBDST[i] = scb[i];
    for (i = 0; i < PIX_BYTES; i++) PIXDST[i] = pix[i];
}

static void reveal_panel(void)
{
    long i;
    unsigned char *pal = g_blob + PIX_BYTES + SCB_BYTES;
    for (i = 0; i < PAL_BYTES; i++) PALDST[i] = pal[i];
}

/* full repaint of the MAIN dashboard (used at boot and when CONFIG closes) */
static void paint_main(void)
{
    cfg_apply_runtime();
    show_panel();
    draw_static();
    draw_numbers((g_gavg + 500UL) / 1000UL);
    draw_graph();
    draw_scope();
    reveal_panel();
}

static int getkey(void)
{
    int k;
    if (!(KBD[0] & 0x80)) return -1;
    k = KBD[0] & 0x7F; STROBE[0] = 0;
    return k;
}

/* ---- self-drawn mouse cursor (direct VRAM, bypasses g_drawbuf/QuickDraw) ---- */
static unsigned char getpix_raw(int x, int y)
{
    unsigned char b;
    if (x < 0 || x > 319 || y < 0 || y > 199) return 0;
    b = PIXDST[(long)y * 160L + (x >> 1)];
    return (unsigned char)((x & 1) ? (b & 0x0F) : (b >> 4));
}

static void putpix_raw(int x, int y, unsigned char c)
{
    unsigned char *p;
    if (x < 0 || x > 319 || y < 0 || y > 199) return;
    p = PIXDST + (long)y * 160L + (x >> 1);
    if (x & 1) *p = (unsigned char)((*p & 0xF0) | (c & 0x0F));
    else       *p = (unsigned char)((*p & 0x0F) | ((c & 0x0F) << 4));
}

static void cursor_hide(void)        /* restore the pixels the cursor was covering */
{
    int cx, cy;
    if (!g_cshown) return;
    for (cy = 0; cy < CUR_H; cy++)
        for (cx = 0; cx < CUR_W; cx++)
            putpix_raw(g_cx + cx, g_cy + cy, g_cunder[cy * CUR_W + cx]);
    g_cshown = 0;
}

static void cursor_show(void)        /* save background, then stamp the arrow */
{
    int cx, cy;
    unsigned char v;
    g_cur_dirty = 0;
    if (g_cshown) return;
    for (cy = 0; cy < CUR_H; cy++)
        for (cx = 0; cx < CUR_W; cx++)
            g_cunder[cy * CUR_W + cx] = getpix_raw(g_cx + cx, g_cy + cy);
    for (cy = 0; cy < CUR_H; cy++)
        for (cx = 0; cx < CUR_W; cx++) {
            v = CUR_BMP[cy][cx];
            if (v) putpix_raw(g_cx + cx, g_cy + cy, (unsigned char)(v == 1 ? C_BLACK : C_WHITE));
        }
    g_cshown = 1;
}

/* does the cursor box intersect rect (x,y,w,h)? */
static int cursor_overlaps(int x, int y, int w, int h)
{
    if (g_cx + CUR_W <= x || g_cx >= x + w) return 0;
    if (g_cy + CUR_H <= y || g_cy >= y + h) return 0;
    return 1;
}

/* a draw is about to hit (x,y,w,h): lift the pointer iff it overlaps, and flag
 * for a single restamp after the frame's draws complete. */
static void cursor_clear_for(int x, int y, int w, int h)
{
    if (g_cshown && cursor_overlaps(x, y, w, h)) { cursor_hide(); g_cur_dirty = 1; }
}

static int in_rect(int px, int py, int x, int y, int w, int h)
{
    return (px >= x && px < x + w && py >= y && py < y + h);
}

/* After any PosMouse teleport, latch the CURRENT physical button into g_pbtn so a
 * still-held click (e.g. the one that opened this screen) is NOT re-fired as a fresh
 * press at the pointer's new location. Without this the CONFIG click flip-flops:
 * open -> phantom SAVE -> return -> phantom CONFIG -> ... until the button releases. */
static void mouse_seed_btn(void)
{
    MouseRec m = ReadMouse();
    g_pbtn = (m.mouseStatus & 0x80) ? 1 : 0;
}

static void try_start_run(void)
{
    if (g_running) return;
    if (!g_demo && strat_state() != STR_MINING) return;      /* live needs a pool job in hand */
    g_running = 1;
    draw_state();
}

/* act on a mouse-down at (px,py) in 320-screen pixels. Caller has hidden the cursor. */
static void mouse_click(int px, int py)
{
    if (in_rect(px, py, BTN_RUN_X, BTN_RUN_Y, BTN_RUN_W, BTN_RUN_H)) {
        try_start_run();
    } else if (in_rect(px, py, BTN_STOP_X, BTN_STOP_Y, BTN_STOP_W, BTN_STOP_H)) {
        if (g_running) { g_running = 0; g_gavg = 0; g_gsm = 0; draw_state(); }  /* readout snaps to 0 */
    } else if (in_rect(px, py, BTN_CFG_X, BTN_CFG_Y, BTN_CFG_W, BTN_CFG_H)) {
        press_flash(BTN_CFG_X, BTN_CFG_Y, BTN_CFG_W, BTN_CFG_H, CONFIG_GREY, "CONFIG", C_WHITE);
        g_open_cfg = 1;        /* serviced by the main loop (outside the click context) */
    }
}

/* ===== CONFIG page (Stage 3): drawing/handling below; the config LOGIC (persistence,
 * validation, Marinetti probe, runtime apply) lives in cfg.c (see cfg.h). ===== */

/* status text in the title screen (clears the black recess, redraws centred) */
static void cfg_title(const char *s, unsigned char c)
{
    clearblk(11, 10, 298, 13);
    text_c(10, 300, 13, s, c);
}

/* default title bar (demo vs live); validation errors replace this temporarily.
 * In LIVE the well doubles as a connection status line: if the pool socket isn't
 * healthy it explains the fault and points the user to the fix (or to DEMO). The
 * Stratum state is frozen while we're on this page (strat_poll runs on main), so
 * the message reflects the state at the moment CONFIG was opened. */
static void cfg_title_default(void)
{
    if (g_demo) {
        cfg_title("DEMO MODE - SET TCP/POOL/WALLET FOR LIVE", CF_CYAN);
        return;
    }
    switch (strat_state()) {
    case STR_CONN_FAIL: cfg_title("POOL UNREACHABLE - CHECK ADDRESS OR USE DEMO", CF_RED); break;
    case STR_DNS_FAIL:  cfg_title("CANNOT RESOLVE POOL NAME - CHECK DNS OR USE IP", CF_RED); break;
    case STR_POOL_FAIL: cfg_title("POOL ERROR - CHECK WORKER/WALLET OR USE DEMO", CF_RED); break;
    case STR_NO_IP:     cfg_title("TCP/IP NOT CONNECTED - FIX OR USE DEMO",   CF_RED);   break;
    case STR_NO_TCP:    cfg_title("MARINETTI NOT LOADED - USE DEMO MODE",     CF_RED);   break;
    case STR_RESOLVING: cfg_title("RESOLVING POOL NAME ...",                  CF_CYAN);  break;
    case STR_OPENING:
    case STR_RETRY:     cfg_title("CONNECTING TO POOL ...",                   CF_CYAN);  break;
    case STR_CONNECTED:
    case STR_SUBSCRIBED:cfg_title("SUBSCRIBING TO POOL ...",                  CF_CYAN);  break;
    case STR_MINING:    cfg_title(strat_on_backup() ? "LIVE MODE - MINING ON BACKUP POOL"
                                                     : "LIVE MODE - MINING ON POOL", CF_GREEN); break;
    default:            cfg_title("GS MINER  -  CONFIGURATION",               CF_CYAN);  break;
    }
}

/* MODE row: LIVE | DEMO pair; cfg_mode still in MINER.CONF */
static void cfg_screw(int cx, int cy)
{
    int i, j;
    for (j = -2; j <= 2; j++)
        for (i = -2; i <= 2; i++)
            if (i * i + j * j <= 4)
                setpix(cx + i, cy + j, CF_GREY);
    setpix(cx - 1, cy - 1, CF_WHITE);
    setpix(cx, cy, CF_WHITE);
    setpix(cx - 1, cy, CF_BLACK);
    setpix(cx, cy, CF_BLACK);
    setpix(cx + 1, cy, CF_BLACK);
}

static void cfg_draw_screws(void)
{
    cfg_screw(CFG_SCREW_LX, CFG_SCREW_TOP_Y);
    cfg_screw(CFG_SCREW_RX, CFG_SCREW_TOP_Y);
    cfg_screw(CFG_SCREW_LX, CFG_SCREW_BTM_Y);
    cfg_screw(CFG_SCREW_RX, CFG_SCREW_BTM_Y);
}

static void cfg_draw_mode_btns(void)
{
    cursor_clear_for(CF_MODE_X, CF_MODE_Y, CF_BTN_W, CF_BTN_H);
    button(CF_MODE_X, CF_MODE_Y, CF_MODE_W, CF_BTN_H, CF_BLUE, "LIVE",
           CF_WHITE, !g_demo);
    button(CF_MODE_X + 52, CF_MODE_Y, CF_MODE_W, CF_BTN_H, CF_AMBER, "DEMO",
           CF_BLACK, g_demo);
}

static void cfg_set_live(void)
{
    const char *m;
    if (!g_demo) return;
    g_cfg_badf = -1;
    m = cf_live_block_msg();
    if (m) {
        cursor_hide();
        cfg_title(m, CF_RED);
        cfg_draw_mode_btns();
        cursor_show();
        return;
    }
    strcpy(cfg_mode, "LIVE");
    cfg_sync_demo();
    cursor_hide();
    cfg_title_default();
    cfg_draw_mode_btns();
    cursor_show();
}

static void cfg_set_demo(void)
{
    if (g_demo) return;
    g_cfg_badf = -1;
    strcpy(cfg_mode, "DEMO");
    cfg_sync_demo();
    cursor_hide();
    cfg_title_default();
    cfg_draw_mode_btns();
    cursor_show();
}

static void cfg_draw_field(int i, int focused);

/* redraw editable fields (1..6) + LIVE|DEMO buttons */
static void cfg_redraw_all(int foc)
{
    int i;
    for (i = 1; i < CF_NF; i++) cfg_draw_field(i, i == foc);
    cfg_draw_mode_btns();
}
static void cfg_draw_field(int i, int focused)
{
    int wx = cf_wx[i], wy = cf_wy[i], ww = cf_ww[i];
    int ty = wy + (CF_ROW_H - GLYPH_H) / 2;
    unsigned char col = (i == g_cfg_badf) ? CF_RED : CF_CYAN;
    region_fill(wx, wy, ww, CF_ROW_H, CF_BLUE);
    g_drawbuf = 1;
    if (i == 2) {                                /* WALLET: legible 5x7 mixed case, inset */
        int yy, x = wallet_text(wx + 4, ty, cf_buf[i], col, ww - 6);
        if (focused && x < wx + ww - 1)
            for (yy = ty - 1; yy < ty + FW_H; yy++) setpix(x, yy, CF_CYAN);
    } else {
        int tx = wx + 4, x = tx, yy;
        const char *s = cf_buf[i];
        while (*s && (x - tx) + GLYPH_W <= ww - 6) x = drawchar(x, ty, *s++, col);
        if (focused && x < wx + ww - 1)
            for (yy = ty - 1; yy < ty + GLYPH_H + 1; yy++) setpix(x + 1, yy, CF_CYAN);
    }
    g_drawbuf = 0;
    region_blit();
}

/* fixed chrome: title, field labels, KEYS help, SAVE/DEFAULTS/CANCEL/QUIT caps */
static void cfg_draw_static(void)
{
    cfg_title_default();
    text(CF_LBL_X + 4, CF_LBL_Y(CF_ROW0), "WORKER", CF_BLACK);
    text(CF_LBL_X + 4, CF_LBL_Y(CF_ROW1), "WALLET", CF_BLACK);
    text(CF_LBL_X + 4, CF_LBL_Y(CF_ROW2), "POOL",   CF_BLACK);
    text(CF_LBL_X + 4, CF_LBL_Y(CF_ROW3), "BACKUP", CF_BLACK);
    text(CF_PORT_LBL_X + 3, CF_LBL_Y(CF_ROW2), "PORT", CF_BLACK);
    text(CF_PORT_LBL_X + 3, CF_LBL_Y(CF_ROW3), "PORT", CF_BLACK);
    text(12, 106, "DEMO: LOCAL SHA-256 + NO POOL/PAYOUT", CF_AMBER);
    text(12, 115, "LIVE: MARINETTI + TCP/IP REQUIRED",    CF_AMBER);
    text(12, 133, "CLICK LIVE OR DEMO TO TOGGLE",         CF_AMBER);
    text(12, 151, "TAB",    CF_AMBER); text(68, 151, "NEXT FIELD",  CF_AMBER);
    text(12, 160, "RETURN", CF_AMBER); text(68, 160, "SAVE + EXIT", CF_AMBER);
    text(12, 169, "ESC",    CF_AMBER); text(68, 169, "QUIT",        CF_AMBER);
    text(12, 178, "DELETE", CF_AMBER); text(68, 178, "ERASE CHAR",  CF_AMBER);
    button(CF_SAVE_X, CF_SAVE_Y, CF_BTN_W, CF_BTN_H, CF_GREEN, "SAVE",     CF_BLACK, 0);
    button(CF_DEF_X,  CF_DEF_Y,  CF_BTN_W, CF_BTN_H, CF_GREY,  "DEFAULTS", CF_BLACK, 0);
    button(CF_CAN_X,  CF_CAN_Y,  CF_CAN_W, CF_BTN_H, CF_GREY,  "CANCEL",   CF_WHITE, 0);
    button(CF_QUIT_X, CF_QUIT_Y, CF_QUIT_W, CF_BTN_H, CF_RED,  "QUIT",     CF_WHITE, 0);
    cfg_draw_mode_btns();
    cfg_draw_screws();
}

/* hit-test: 1..CF_NF-1 = field, 104 LIVE, 105 DEMO, 100 SAVE, 101 CANCEL, 102 QUIT, 103 DEFAULTS */
static int cfg_hit(int px, int py)
{
    int i;
    for (i = 1; i < CF_NF; i++)
        if (in_rect(px, py, cf_wx[i], cf_wy[i], cf_ww[i], CF_ROW_H)) return i;
    if (in_rect(px, py, CF_SAVE_X, CF_SAVE_Y, CF_BTN_W,  CF_BTN_H)) return 100;
    if (in_rect(px, py, CF_DEF_X,  CF_DEF_Y,  CF_BTN_W,  CF_BTN_H)) return 103;
    if (in_rect(px, py, CF_CAN_X,  CF_CAN_Y,  CF_CAN_W,  CF_BTN_H)) return 101;
    if (in_rect(px, py, CF_QUIT_X, CF_QUIT_Y, CF_QUIT_W, CF_BTN_H)) return 102;
    if (in_rect(px, py, CF_MODE_X, CF_MODE_Y, CF_MODE_W, CF_BTN_H)) return CF_HIT_LIVE;
    if (in_rect(px, py, CF_MODE_X + 52, CF_MODE_Y, CF_MODE_W, CF_BTN_H)) return CF_HIT_DEMO;
    return -1;
}

/* clear any red error flag + restore the normal title (called when the user edits) */
static void cfg_clear_error(int foc)
{
    int bad = g_cfg_badf;
    if (bad < 0) return;
    g_cfg_badf = -1;
    cursor_hide();
    cfg_title_default();
    if (bad == 0) cfg_draw_mode_btns();
    else          cfg_draw_field(bad, bad == foc);
    cursor_show();
}

/* SAVE-time DNS check: resolve a pool HOSTNAME via Marinetti's DNR so a typo /
 * dead name fails right here in config (clear note) instead of silently later on
 * the main page. Dotted-quad IPs skip it. Bounded (~12s) so a stalled DNS can't
 * wedge the modal; we pump TCPIPPoll while waiting. Only valid in LIVE (link was
 * already confirmed up by cfg_validate). 1 = ok (literal IP or resolved), 0 = no. */
static int cfg_dns_ok(const char *host)
{
    static dnrBuff dnr;
    static char    pstr[260];
    LongWord       deadline;
    int            n;

    if (cf_all_digit_dot(host)) return 1;        /* literal IP -> nothing to resolve */

    n = (int)strlen(host);
    if (n < 1 || n > 255) return 0;
    pstr[0] = (char)n;                           /* DNR wants a Pascal string */
    memcpy(pstr + 1, host, (unsigned)n);
    dnr.DNRstatus = DNR_Pending;
    TCPIPDNRNameToIP((Ref)pstr, (Ref)&dnr);
    if (toolerror()) return 0;

    deadline = GetTick() + 12UL * TICKS_PER_SEC;
    while (dnr.DNRstatus == DNR_Pending) {
        TCPIPPoll();
        if (GetTick() >= deadline) { TCPIPCancelDNR((Ref)&dnr); return 0; }
    }
    return dnr.DNRstatus == DNR_OK;
}

/* flag a field as the offender: red title, focus + repaint it, restore cursor. */
static int cfg_save_reject(int *foc, int field, const char *msg)
{
    int old = *foc;
    g_cfg_badf = field;
    cfg_title(msg, CF_RED);
    *foc = field;
    cfg_draw_field(old, old == field);
    cfg_draw_field(field, 1);
    cursor_show();
    return 0;
}

/* status for the SAVE-time DNS step: name the FIELD we're resolving (PRIMARY / BACKUP)
 * and echo the host (upper-cased to match the font, truncated to fit the well) so the
 * message tracks whichever name is actually being looked up - not a fixed label. */
static void cfg_dns_msg(char *out, const char *which, const char *host)
{
    const char *pfx = "RESOLVING ";
    int i = 0, j;
    while (pfx[i]) { out[i] = pfx[i]; i++; }
    for (j = 0; which[j]; j++) out[i++] = which[j];
    out[i++] = ':'; out[i++] = ' ';
    for (j = 0; host[j] && i < 48; j++) {
        char c = host[j];
        if (c >= 'a' && c <= 'z') c = (char)(c - 32);   /* uppercase glyphs only */
        out[i++] = c;
    }
    out[i] = '\0';
}

/* attempt SAVE: validate, resolve pool name(s), then persist. 1 on success. */
static int cfg_try_save(int *foc)
{
    int bad = -1;
    const char *err = cfg_validate(&bad);
    if (err) {                              /* invalid: focus + red-flag the offender */
        g_cfg_badf = bad;
        cursor_hide();
        cfg_title(err, CF_RED);
        if (bad == 0) cfg_draw_mode_btns();
        else {
            int old = *foc; *foc = bad;
            cfg_draw_field(old, old == bad); cfg_draw_field(bad, 1);
        }
        cursor_show();
        return 0;
    }
    cfg_sync_demo();
    if (!g_demo) {                          /* LIVE: a name we can't resolve = bad save */
        char dmsg[64];
        cursor_hide();
        cfg_dns_msg(dmsg, "PRIMARY", cfg_pool);
        cfg_title(dmsg, CF_CYAN);
        if (!cfg_dns_ok(cfg_pool))
            return cfg_save_reject(foc, 3, "POOL: CANNOT RESOLVE NAME");
        if (cfg_back[0]) {
            cfg_dns_msg(dmsg, "BACKUP", cfg_back);
            cfg_title(dmsg, CF_CYAN);
            if (!cfg_dns_ok(cfg_back))
                return cfg_save_reject(foc, 5, "BACKUP: CANNOT RESOLVE NAME");
        }
        cursor_show();
    }
    if (!cfg_save_file()) {                 /* validated but the write failed */
        cursor_hide(); cfg_title("SAVE FAILED - DISK?", CF_RED); cursor_show();
        return 0;
    }
    return 1;
}

/* Modal config editor. Returns 0 = CANCEL, 1 = SAVE, 2 = QUIT APP. Leaves the MAIN
 * plate repainted (cursor hidden) so the caller just re-parks + re-shows the pointer. */
static int cfg_screen(void)
{
    char snap[CF_NF][64];
    int i, foc = 1, ret = -1, k;

    g_cfg_badf = -1;
    for (i = 0; i < CF_NF; i++) strcpy(snap[i], cf_buf[i]);   /* for CANCEL */

    cursor_hide();
    if (!load_blob_path(miner_sysfile("CONFIG"))) { load_blob(); paint_main(); return 0; }
    show_panel();
    cfg_draw_static();
    cfg_redraw_all(foc);
    reveal_panel();

    g_cx = CF_VAL_X + 20; g_cy = CF_LBL_Y(CF_ROW0); g_cshown = 0;   /* park on WORKER */
    PosMouse(g_cx, g_cy);
    mouse_seed_btn();                       /* ignore the still-held opening click */
    cursor_show();

    while (ret < 0) {
        MouseRec m = ReadMouse();
        int px = (int)((unsigned)m.xPos >> MOUSE_XSHIFT);
        int py = (int)m.yPos;
        int btn = (m.mouseStatus & 0x80) ? 1 : 0;
        if (px > 319 - CUR_W) px = 319 - CUR_W; if (px < 0) px = 0;
        if (py > 199 - CUR_H) py = 199 - CUR_H; if (py < 0) py = 0;

        if (btn && !g_pbtn) {
            int hit;
            cursor_hide();
            hit = cfg_hit(px, py);
            if (hit >= 1 && hit < CF_NF) {
                if (hit != foc) { int old = foc; foc = hit;
                                  cfg_draw_field(old, 0); cfg_draw_field(foc, 1); }
            } else if (hit == CF_HIT_LIVE) {
                cfg_set_live();
            } else if (hit == CF_HIT_DEMO) {
                cfg_set_demo();
            } else if (hit == 100) {                          /* SAVE */
                press_flash(CF_SAVE_X, CF_SAVE_Y, CF_BTN_W, CF_BTN_H, CF_GREEN, "SAVE", CF_BLACK);
                g_cx = px; g_cy = py; cursor_show();
                if (cfg_try_save(&foc)) { ret = 1; }
                g_pbtn = btn; continue;
            } else if (hit == 101) {                          /* CANCEL */
                press_flash(CF_CAN_X, CF_CAN_Y, CF_CAN_W, CF_BTN_H, CF_GREY, "CANCEL", CF_WHITE);
                ret = 0;
            } else if (hit == 102) {                          /* QUIT */
                press_flash(CF_QUIT_X, CF_QUIT_Y, CF_QUIT_W, CF_BTN_H, CF_RED, "QUIT", CF_WHITE);
                ret = 2;
            } else if (hit == 103) {                            /* DEFAULTS */
                press_flash(CF_DEF_X, CF_DEF_Y, CF_BTN_W, CF_BTN_H, CF_GREY, "DEFAULTS", CF_BLACK);
                g_cfg_badf = -1;
                for (i = 0; i < CF_NF; i++) strcpy(cf_buf[i], CF_DEF[i]);
                cfg_sync_demo();
                cfg_title_default();
                cfg_redraw_all(foc);
            }
            g_cx = px; g_cy = py; cursor_show();
        } else if (px != g_cx || py != g_cy) {
            cursor_hide(); g_cx = px; g_cy = py; cursor_show();
        }
        g_pbtn = btn;
        if (ret >= 0) break;

        k = getkey();
        if (k < 0) continue;
        if (k == 0x1B) { ret = 2; break; }                    /* ESC = QUIT app */
        if (k == 0x0D) { if (cfg_try_save(&foc)) { ret = 1; break; } continue; }  /* RETURN = save */
        if (k == 0x09) {                                      /* TAB = next editable field */
            int old = foc; foc = 1 + (foc - 1 + 1) % (CF_NF - 1);
            cursor_hide(); cfg_draw_field(old, 0); cfg_draw_field(foc, 1); cursor_show();
            continue;
        }
        if (k == 0x08 || k == 0x7F) {                         /* BS / DELETE = erase */
            int n = (int)strlen(cf_buf[foc]);
            cfg_clear_error(foc);
            if (n > 0) { cf_buf[foc][n - 1] = 0;
                         cursor_hide(); cfg_draw_field(foc, 1); cursor_show(); }
            continue;
        }
        if (k >= 0x20 && k <= 0x7E) {                         /* printable -> append */
            int n = (int)strlen(cf_buf[foc]);
            cfg_clear_error(foc);
            if (n < cf_max[foc]) { cf_buf[foc][n] = (char)k; cf_buf[foc][n + 1] = 0;
                                   cursor_hide(); cfg_draw_field(foc, 1); cursor_show(); }
            continue;
        }
    }

    if (ret != 1) for (i = 0; i < CF_NF; i++) strcpy(cf_buf[i], snap[i]);  /* CANCEL/QUIT discard */

    g_cfg_badf = -1;
    cursor_hide();
    if (ret != 2) { load_blob(); paint_main(); }   /* QUIT: skip repaint, app is exiting */
    return ret;
}

int main(void)
{
    Handle hs, hb;
    struct sha256_context **ch;
    LongWord win_tick, scope_tick, move_deadline, stat_tick, now;
    unsigned long last_hashes = 0, hps = 0;
    int k, last_running = 1;

    paths_init();                            /* resolve <appdir>/SYSFILES/ before any file I/O */

    hb = NewHandle(BLOB_BYTES, userid(), attrLocked | attrFixed, 0L);
    if (toolerror()) { printf("NewHandle blob (%04x)\n", toolerror()); return 1; }
    g_blob = (unsigned char *)*hb;
    if (!load_blob()) { DisposeHandle(hb); return 1; }

    hs = NewHandle(SHR_SAVE, userid(), attrLocked | attrFixed, 0L);
    if (toolerror()) { printf("NewHandle save (%04x)\n", toolerror()); DisposeHandle(hb); return 1; }
    g_save = (unsigned char *)*hs;

    /* SHA context: bank 0, page+bank aligned, no bank crossing (see minecore.c) */
    ch = (struct sha256_context **)NewHandle(sizeof(struct sha256_context), userid(),
            attrFixed | attrPage | attrBank | attrNoCross, 0x000000L);
    if (toolerror()) { printf("NewHandle ctx (%04x)\n", toolerror()); DisposeHandle(hs); DisposeHandle(hb); return 1; }
    g_ctx = *ch;

    build_job();

    mlog_open();                             /* arm the diag log BEFORE any network activity */
    mlog_event("== GS MINER %s ==", APPVER);

    cfg_net_bringup();                       /* load + start Marinetti for this app, log tool errors */

    cfg_load_file();                         /* restore config (in LIVE this begins a connect) */
    g_strat_last = strat_state();

    mlog_event("MODE %s", g_demo ? "DEMO" : "LIVE");
    if (!g_demo)
        mlog_event("CFG pool=%s:%s backup=%s:%s worker=%s",
                   cfg_pool, cfg_pport, cfg_back[0] ? cfg_back : "-",
                   cfg_back[0] ? cfg_bport : "-", cfg_worker);
    /* NB: the Marinetti net-config dump (IP/DNS/link) is logged once we reach MINING,
     * NOT here - cfg_load_file() may have a DNR lookup in flight at this point, and
     * hitting Marinetti (TCPIPGetDNS etc.) on top of that hung the machine at startup. */

    save_desktop();
    show_panel();
    draw_static();

    g_start = GetTick(); win_tick = g_start; scope_tick = g_start; stat_tick = g_start; move_deadline = 0;
    g_run_start = g_start; g_run_frozen = 0;     /* uptime counts the current run, from 0 */
    g_gavg = 0; g_gsm = 0;
    { int i; for (i = 0; i < GR_W; i++) g_graph[i] = 0; }
    draw_numbers(0);
    draw_graph();
    draw_scope();
    reveal_panel();                          /* flash the finished dashboard in */

    SetMouse(MOUSE_TRANSP);                  /* take the mouse: poll position ourselves */
    ClampMouse(0, 319, 0, 199);              /* live coords arrive already in 320x200 space */
    PosMouse(246, 28);                       /* park off to the side: gap right of the value wells */
    g_cx = 246; g_cy = 28; g_pbtn = 0; g_cshown = 0;
    mouse_seed_btn();                        /* latch real button state at boot */
    cursor_show();

    for (;;) {
        now = GetTick();

        /* ---- poll the pointer EVERY pass so it tracks between hashes ---- */
        {
            MouseRec m = ReadMouse();
            int px = (int)((unsigned)m.xPos >> MOUSE_XSHIFT);
            int py = (int)m.yPos;
            int btn = (m.mouseStatus & 0x80) ? 1 : 0;          /* bit7 = button down (flip if inverted) */
            if (px > 319 - CUR_W) px = 319 - CUR_W; if (px < 0) px = 0;
            if (py > 199 - CUR_H) py = 199 - CUR_H; if (py < 0) py = 0;
            if (btn && !g_pbtn) {                              /* click on the press edge */
                cursor_hide(); mouse_click(px, py);
                g_cx = px; g_cy = py; cursor_show();
                move_deadline = now + MOVE_COAST;
            } else if (px != g_cx || py != g_cy) {             /* moved: relocate + extend the coast */
                cursor_hide(); g_cx = px; g_cy = py; cursor_show();
                move_deadline = now + MOVE_COAST;
            }
            g_pbtn = btn;
        }

        if (g_open_cfg) {                       /* CONFIG button -> modal editor */
            int cr;
            g_open_cfg = 0;
            cr = cfg_screen();                  /* returns on the repainted MAIN plate */
            if (cr == 2) break;                 /* QUIT chosen on the config page */
            if (cr == 1)                        /* saved: record the new operating config */
                mlog_event("RECONFIG %s pool=%s:%s backup=%s:%s", g_demo ? "DEMO" : "LIVE",
                           cfg_pool, cfg_pport, cfg_back[0] ? cfg_back : "-",
                           cfg_back[0] ? cfg_bport : "-");
            g_cx = BTN_CFG_X + 25; g_cy = BTN_CFG_Y + 6; g_cshown = 0;
            PosMouse(g_cx, g_cy); mouse_seed_btn(); cursor_show();   /* don't re-fire held click */
            now = GetTick();                    /* drop the stale window so no dt spike */
            win_tick = now; scope_tick = now; stat_tick = now; move_deadline = now;
            last_hashes = g_hashes; last_running = g_running;
            continue;
        }

        /* Hash a slice only once the pointer has been at rest past the coast window.
         * While you're aiming (moving, with sub-second pauses) the loop spins free and
         * the cursor is buttery; ~0.75s after you settle, full-speed hashing resumes. */
        if (g_running && now >= move_deadline) mine_slice();

        k = getkey();
        if (k == 0x1B || k == 'Q' || k == 'q') break;          /* ESC / Q */
        if (k == ' ') { cursor_hide();
                        if (g_running) { g_running = 0; g_gavg = 0; g_gsm = 0; }
                        else try_start_run();
                        draw_state(); cursor_show(); }

        now = GetTick();                                        /* refresh after the (possible) hash */

        if (!g_demo) {
            int st = strat_poll();                  /* advances one step; services TCPIPPoll */
            if (st != g_strat_last) {
                int was_mining = (g_strat_last == STR_MINING);
                int now_mining = (st == STR_MINING);
                int run_was = g_running;
                g_strat_last = st;
                cursor_hide();
                if (now_mining && !was_mining)        g_running = 1;            /* job in hand: mine */
                else if (!now_mining && was_mining && g_running) {              /* lost job: stop */
                    g_running = 0; g_gavg = 0; g_gsm = 0;
                }
                g_tcp = strat_to_tcp(st);
                if (g_running != run_was) draw_state();   /* only when RUN/STOP flips: no poll flicker */
                draw_tcp();
                draw_mode_badge();
                draw_pool_fields();                   /* highlight the live pool (PRI vs FAILOVER) */
                cursor_show();
            }
        }

        if (g_running != last_running) {                       /* RUN/STOP edge: refresh at once */
            if (g_running) g_run_start = now;                   /* RUN: session clock restarts at 0 */
            else           g_run_frozen = (now - g_run_start) / TICKS_PER_SEC;  /* STOP: freeze uptime */
            draw_scope();                                       /* scope -> dark on stop, live on run */
            draw_graph();                                       /* line -> zero baseline on stop */
            scope_tick = now;
            last_running = g_running;
        }

        if (now - win_tick >= REDRAW_TICKS) {                  /* numbers + graph (~0.5s) */
            LongWord el = now - win_tick;
            unsigned long dh = g_hashes - last_hashes;
            unsigned long grate = el ? (dh * TICKS_PER_SEC * 1000UL / el) : 0; /* milli-H/s this window */
            last_hashes = g_hashes; win_tick = now;
            graph_push(grate);                                  /* fold sample into the smoothed centre */
            /* show the SMOOTHED rate, not the bouncy 0.5s sample: at ~10 H/s a single
             * hash swings the raw window 2<->11, but the true speed is steady. This also
             * steadies the ETA/odds gag (no wild spikes when the sample dips to 2). */
            hps = (g_gavg + 500UL) / 1000UL;
            draw_numbers(hps);                                  /* self-gates the pointer per blit */
            draw_graph();
        }

        if (now - scope_tick >= SCOPE_TICKS) {                 /* heavy scope (~1.5s) */
            draw_scope();
            scope_tick = now;
        }

        if (now - stat_tick >= STAT_TICKS) {                   /* on-disk STAT heartbeat (~15s) */
            unsigned long up = g_running ? (unsigned long)((now - g_run_start) / TICKS_PER_SEC)
                                         : (unsigned long)g_run_frozen;
            mlog_stat(hps, g_bestbits, strat_accepted(), strat_rejected(),
                      strat_on_backup(), up);
            stat_tick = now;
        }

        if (g_cur_dirty) cursor_show();                        /* one restamp if a repaint lifted it */
    }

    mlog_close();                            /* final BYE line for a clean exit */
    cursor_hide();                           /* lift pointer before touching SHR memory */
    restore_desktop();                       /* SHR off -> copy -> SHR/mode back */
    SetMouse(0x00);                          /* hand mouse back after desktop is restored */
    DisposeHandle((Handle)ch); DisposeHandle(hs); DisposeHandle(hb);
    return 0;
}
