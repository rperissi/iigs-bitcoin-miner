/*
 * cfg.c - CONFIG subsystem (persistence, validation, Marinetti probe, runtime apply),
 * split out of viz.c for modularity (it was also a convenient way to keep viz.c's own
 * translation unit a bit smaller). Built with the large memory model (occ -b) like the
 * rest of the project.
 *
 * Build: occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c paths.c -L. -llib65816hash -o viz
 */
#include <stdio.h>
#include <string.h>
#include <types.h>
#include <orca.h>
#include <misctool.h>
#include <locator.h>
#include "sha256.h"
#include "tcpip.h"
#include "stratum.h"
#include "mine.h"
#include "cfg.h"
#include "paths.h"
#include "mlog.h"

#define TCPIP_TOOL 54                 /* Marinetti TCP/IP tool set = tool $36 (TOOL054) */

/* Bring Marinetti up for THIS application before we probe/use it. We used to assume the
 * TCP/IP tool set was already resident (it is in our emulator boot), but on real hardware
 * a *connected* stack could still read as "MARINETTI NOT FOUND" because the tool set was
 * never loaded/started into our context - so every TCPIP call returned toolNotFoundErr.
 *
 * LoadOneTool() makes the set resident; TCPIPStartUp() registers this app as a user. Both
 * are safe no-ops if Marinetti is not installed (they just set toolerror, which we log so
 * a failed LIVE attempt records the exact code in MINER.LOG). We deliberately do NOT call
 * the rich getters here - those hung the machine in earlier testing (see mlog.c). */
void cfg_net_bringup(void)
{
    Boolean up;

    LoadOneTool(TCPIP_TOOL, 0x0200);              /* resident? load it (min ver 2.0) */
    mlog_event("NET loadtool err=%04x", toolerror());

    TCPIPStartUp();                               /* required before other TCPIP calls */
    mlog_event("NET startup  err=%04x", toolerror());

    up = TCPIPGetConnectStatus();                 /* one-shot probe, logged for diagnosis */
    mlog_event("NET probe connstat=%s err=%04x", up ? "Y" : "N", toolerror());
}

int cf_mode_token(const char *s)
{
    return !strcmp(s, "DEMO") || !strcmp(s, "LIVE");
}

/* Marinetti probe: toolerror after call = stack missing; Boolean = IP link up */
int cf_net_state(void)
{
    Boolean up = TCPIPGetConnectStatus();
    if (toolerror()) return NET_NONE;
    return up ? NET_UP : NET_NO_LINK;
}

const char *cf_live_block_msg(void)
{
    int st = cf_net_state();
    if (st == NET_NONE)    return "LIVE: MARINETTI NOT FOUND";
    if (st == NET_NO_LINK) return "LIVE: TCP/IP NOT CONNECTED";
    return 0;
}

/* map a Stratum state to the TCP lamp colour (TCP_OFF grey / DOWN red / UP green) */
int strat_to_tcp(int st)
{
    if (st == STR_MINING) return TCP_ACT;                       /* mining: pulse the lamp */
    if (st == STR_CONNECTED || st == STR_SUBSCRIBED) return TCP_UP;  /* link up, handshaking */
    if (st == STR_NO_TCP || st == STR_OFF) return TCP_OFF;
    return TCP_DOWN;   /* NO_IP / RESOLVING / OPENING / RETRY / *_FAIL */
}

/* uppercase cfg_mode; sync g_demo from saved mode (demo forces the lamp grey) */
void cfg_sync_demo(void)
{
    int i;
    for (i = 0; cfg_mode[i]; i++)
        if (cfg_mode[i] >= 'a' && cfg_mode[i] <= 'z')
            cfg_mode[i] = (char)(cfg_mode[i] - 32);
    g_demo = strcmp(cfg_mode, "LIVE") ? 1 : 0;
    if (g_demo) g_tcp = TCP_OFF;
}

/* after load/save: DEMO auto-mines locally; LIVE starts connecting (mining begins
 * once the Stratum client reaches MINING with a pool job, in the main loop). */
void cfg_apply_runtime(void)
{
    cfg_sync_demo();
    if (g_demo) {
        strat_stop();
        build_job();                            /* restore the demo header (LIVE overwrote it) */
        g_live_gen = 0;                          /* force a rebuild next time we go LIVE */
        g_nonce    = 0;
        g_running  = 1;
    } else {
        strat_config(cfg_pool, cfg_pport, cfg_back, cfg_bport, cfg_wallet, cfg_worker);
        strat_start();
        g_live_gen = 0;                          /* rebuild header from the pool's first job */
        g_running  = 0;                          /* main loop runs us on MINING */
        g_tcp = strat_to_tcp(strat_state());
        g_strat_last = strat_state();
    }
}

/* ---- persistence: <appdir>/SYSFILES/MINER.CONF, one field per line, in cf_buf order ---- */
void cfg_load_file(void)
{
    FILE *f = fopen(miner_sysfile("MINER.CONF"), "r");
    char line[80], lines[CF_NF][80];
    int nlines = 0, i, n, j;
    if (!f) { cfg_apply_runtime(); return; }
    while (nlines < CF_NF && fgets(line, (int)sizeof(line), f)) {
        n = (int)strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
        if (n > (int)sizeof(line) - 1) n = (int)sizeof(line) - 1;
        memcpy(lines[nlines], line, (size_t)n);
        lines[nlines][n] = 0;
        nlines++;
    }
    fclose(f);
    if (nlines == 0) { cfg_apply_runtime(); return; }
    if (cf_mode_token(lines[0])) {
        for (i = 0; i < nlines && i < CF_NF; i++) {
            n = (int)strlen(lines[i]);
            if (n > cf_max[i]) n = cf_max[i];
            memcpy(cf_buf[i], lines[i], (size_t)n); cf_buf[i][n] = 0;
        }
    } else {
        /* legacy 6-line file (worker first, no MODE row) */
        strcpy(cfg_mode, "DEMO");
        for (j = 0; j < nlines && j < CF_NF - 1; j++) {
            n = (int)strlen(lines[j]);
            if (n > cf_max[j + 1]) n = cf_max[j + 1];
            memcpy(cf_buf[j + 1], lines[j], (size_t)n); cf_buf[j + 1][n] = 0;
        }
    }
    cfg_apply_runtime();
}

int cfg_save_file(void)
{
    FILE *f = fopen(miner_sysfile("MINER.CONF"), "w");
    int i;
    if (!f) return 0;
    for (i = 0; i < CF_NF; i++) fprintf(f, "%s\n", cf_buf[i]);
    fclose(f);
    return 1;
}

/* ---- validation: IPv4 dotted-quad, hostname (>=1 dot), or numeric port 1..65535 ---- */
int cf_all_digit_dot(const char *s)
{ for (; *s; s++) if (!((*s >= '0' && *s <= '9') || *s == '.')) return 0; return 1; }

static int cf_is_ipv4(const char *s)
{
    int oct = 0, val = 0, dig = 0;
    if (!*s) return 0;
    for (;; s++) {
        if (*s >= '0' && *s <= '9') { val = val * 10 + (*s - '0'); if (++dig > 3 || val > 255) return 0; }
        else if (*s == '.' || *s == 0) { if (!dig) return 0; oct++; if (!*s) break; val = dig = 0; }
        else return 0;
    }
    return oct == 4;
}

/* proper hostname syntax: dot-separated labels (alnum + internal '-'), >= 2 labels,
 * no empty/leading/trailing dot, and an all-alpha TLD of >= 2 chars (so "pool2.ex."
 * and "pool2." fail, "pool2.ex" passes -- we check syntax, not the actual TLD). */
static int cf_is_host(const char *s)
{
    int labels = 0, llen = 0, lalpha = 0;
    char prev = '.';
    const char *p;
    if (!*s) return 0;
    for (p = s; *p; p++) {
        char c = *p;
        if (c == '.') {
            if (llen == 0 || prev == '-') return 0;     /* empty label / label ends in - */
            labels++; llen = 0; lalpha = 0;
        } else if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) {
            if (llen >= 63) return 0; llen++; lalpha++;
        } else if (c >= '0' && c <= '9') {
            if (llen >= 63) return 0; llen++;
        } else if (c == '-') {
            if (llen == 0 || llen >= 63) return 0;       /* label can't start with - */
            llen++;
        } else return 0;
        prev = c;
    }
    if (llen == 0 || prev == '-') return 0;              /* trailing dot / ends in - */
    if (labels < 1) return 0;                            /* need a dot -> TLD present */
    return (llen >= 2 && lalpha == llen);                /* TLD all-alpha, >= 2 chars */
}

int cf_valid_host(const char *s)
{ return cf_all_digit_dot(s) ? cf_is_ipv4(s) : cf_is_host(s); }

/* TCP port for a Stratum pool: 1024..65535 (block well-known/privileged < 1024;
 * no real mining pool runs there) and the 16-bit ceiling 65535. */
int cf_valid_port(const char *s)
{
    long v = 0; const char *p = s;
    if (!*s) return 0;
    for (; *p; p++) { if (*p < '0' || *p > '9') return 0; v = v * 10 + (*p - '0'); if (v > 65535) return 0; }
    return v >= 1024;
}

/* base58 alphabet = alnum minus the visually-ambiguous 0 O I l (Bitcoin legacy) */
static int cf_is_base58(const char *s)
{
    for (; *s; s++) {
        char c = *s;
        if (c == '0' || c == 'O' || c == 'I' || c == 'l') return 0;
        if (!((c >= '1' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    }
    return 1;
}

/* bech32 body = lowercase letters + digits only (a real segwit addr is all-lowercase) */
static int cf_is_bech32(const char *s)
{
    for (; *s; s++) if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9'))) return 0;
    return 1;
}

/* BTC address: legacy base58 (starts 1 or 3, 26-35 chars, base58 charset) or bech32
 * segwit (starts "bc1", 42 or 62 chars, all-lowercase). Validated on the real case. */
const char *cf_valid_wallet(void)
{
    const char *w = cfg_wallet;
    int n = (int)strlen(w);
    if (n == 0) return "WALLET REQUIRED";
    if (w[0] == '1' || w[0] == '3') {            /* P2PKH / P2SH (base58) */
        if (n < 26 || n > 35) return "WALLET: BAD LENGTH (26-35)";
        if (!cf_is_base58(w))  return "WALLET: NOT BASE58";
        return 0;
    }
    if (w[0] == 'b' && w[1] == 'c' && w[2] == '1') {   /* bech32 segwit/taproot */
        if (n != 42 && n != 62) return "WALLET: BECH32 IS 42/62";
        if (!cf_is_bech32(w))   return "WALLET: BECH32 LOWERCASE";
        return 0;
    }
    return "WALLET: START 1 / 3 / BC1";
}

/* check the whole form; on failure return the message + set *bad to the field. */
const char *cfg_validate(int *bad)
{
    const char *werr;
    cfg_sync_demo();
    if (!cf_mode_token(cfg_mode))            { *bad = 0; return "MODE: DEMO OR LIVE"; }
    if (!strcmp(cfg_mode, "LIVE")) {
        const char *m = cf_live_block_msg();
        if (m)                                 { *bad = 0; return m; }
    }
    if (cfg_worker[0] == 0)                  { *bad = 1; return "WORKER REQUIRED"; }
    werr = cf_valid_wallet();
    if (werr)                                { *bad = 2; return werr; }
    if (!cf_valid_host(cfg_pool))            { *bad = 3;
        return cf_all_digit_dot(cfg_pool) ? "POOL: INVALID IP ADDRESS" : "POOL: INVALID HOST"; }
    if (!cf_valid_port(cfg_pport))           { *bad = 4;
        return cfg_pport[0] ? "PORT: USE 1024-65535" : "PORT REQUIRED"; }
    if (cfg_back[0]) {                       /* backup is optional; validate only if set */
        if (!cf_valid_host(cfg_back))        { *bad = 5;
            return cf_all_digit_dot(cfg_back) ? "BACKUP: INVALID IP" : "BACKUP: INVALID HOST"; }
        if (!cf_valid_port(cfg_bport))       { *bad = 6;
            return cfg_bport[0] ? "BACKUP PORT: 1024-65535" : "BACKUP PORT REQUIRED"; }
    }
    *bad = -1;
    return 0;
}
