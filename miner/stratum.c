/*
 * stratum.c - non-blocking Marinetti/Stratum client (the cooperative twin of the
 * blocking miner.c, M3). strat_poll() advances ONE step per call and never blocks,
 * so the dashboard's cursor + SHR panel keep running. Connection stages:
 *
 *   RESOLVING -> OPENING -> CONNECTED -> SUBSCRIBED -> MINING
 *
 * - RESOLVING : a pool given as a hostname is resolved via Marinetti's DNR first.
 * - OPENING   : TCPIPLogin/OpenTCP, wait for ESTABLISHED.
 * - CONNECTED : transient - immediately send mining.subscribe + mining.authorize.
 * - SUBSCRIBED: wait for the subscribe reply (extranonce1) + the first mining.notify.
 * - MINING    : a job is in hand; viz builds the header + grinds nonces and calls
 *               strat_submit() on a share. New notifies bump the job; submit verdicts
 *               feed strat_accepted().
 *
 * Fault policy: retry the pool with an escalating backoff; after FAILOVER_AFTER
 * misses flip to the backup pool (if configured) and keep alternating so whichever
 * recovers first wins (stay mining). Once the whole-episode budget is spent we
 * settle to a terminal fault by CAUSE: CONN_FAIL (socket), DNS_FAIL (name), or
 * POOL_FAIL (connected but the pool is silent / rejects / talks garbage = "POOL ERR").
 * A reconnect always re-runs the handshake. Link faults (no Marinetti / no IP)
 * re-probe on a timer WITHOUT consuming the pool-retry budget.
 *
 * Build: compiled alongside viz.c with the large memory model (occ -b ... viz.c stratum.c ...).
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <types.h>
#include <orca.h>
#include <misctool.h>
#include "tcpip.h"
#include "stratum.h"
#include "mine.h"
#include "mlog.h"

#define MAKE_IP(a,b,c,d) ((LongWord)(a) | ((LongWord)(b) << 8) | \
                          ((LongWord)(c) << 16) | ((LongWord)(d) << 24))
#define TPS            60UL          /* ticks per second */
#define OPEN_SECS      8UL           /* SYN -> ESTABLISHED timeout */
#define DNR_SECS       10UL          /* hostname-resolution timeout */
#define HS_SECS        12UL          /* subscribe -> first job timeout (rides out ckpool's slow first notify) */
#define PROBE_SECS     2UL           /* link re-probe interval when no net */
#define FAILOVER_AFTER 3             /* fails on a pool before flipping to the other */
#define REPROBE_SECS   180UL         /* on backup: quietly single-shot re-test the primary */
#define EASY_BITS      8             /* dev/easy submit target (diff < 1): 1 leading zero byte */
#define NO_SUBMIT_BITS 255           /* "never submit" until a difficulty is known */

/* fail cause -> terminal state */
#define FC_CONN 0                    /* socket: refused / timeout / dropped */
#define FC_DNS  1                    /* name resolution */
#define FC_POOL 2                    /* protocol: no job / reject / garbage */

/* FC_CONN sub-cause, logged so a reader can tell a dead pool from a dead route */
#define CW_NONE    0                 /* generic / local (Login/OpenTCP) */
#define CW_REFUSED 1                 /* SYN -> RST/close: pool not accepting (server-side) */
#define CW_TIMEOUT 2                 /* no response in OPEN_SECS: route black-holed / host down */
#define CW_DROPPED 3                 /* an established socket fell over mid-session */

static int      g_state = STR_OFF;
static Word     g_ipid;
static int      g_open;              /* 1 = we have a live ipid to close */
static char     g_host[64];          /* primary pool: dotted-quad OR hostname */
static Word     g_dport;
static char     g_bhost[64];         /* backup pool ("" = none -> no failover) */
static Word     g_bport;
static char     g_wallet[72];        /* BTC payout address (Stratum username base) */
static char     g_worker[24];        /* worker label, appended as wallet.worker */
static char     g_user[100];         /* composed authorize/submit username */
static int      g_need_bits = NO_SUBMIT_BITS;  /* set from mining.set_difficulty */
static LongWord g_target_ip;         /* resolved/parsed IP of the current attempt */
static int      g_which;             /* 0 = primary, 1 = backup (current target) */
static int      g_try;               /* total connect attempts this failover episode */
static int      g_poolfails;         /* consecutive fails on the current pool */
static LongWord g_reprobe;           /* while on backup: tick to single-shot re-test primary (0=off) */
static int      g_reprobing;         /* 1 = the current attempt is a primary re-probe */
static int      g_syn_seen;          /* SYN got past CLOSED (so CLOSED now = refused) */
static int      g_last_cause;        /* cause of the most recent fail_conn (FC_*) */
static int      g_conn_why;          /* FC_CONN sub-cause for the log: CW_* (refused/timeout/dropped) */
static LongWord g_deadline;          /* OPENING establish / RESOLVING timeout */
static LongWord g_hs_deadline;       /* SUBSCRIBED first-job timeout */
static LongWord g_wait;              /* resume tick for RETRY / link re-probe */
static int      g_netlogged;         /* 1 once we've dumped the net config this session */

static srBuff   sr;
static rrBuff   rr;
static dnrBuff  g_dnr;               /* DNR result record for the in-flight lookup */
static char     g_pstr[260];         /* hostname as a Pascal string for the DNR */
static char     acc[8192];           /* inbound byte buffer (real-pool notify lines
                                        run 1.5-2.5KB: full coinbase + deep merkle) */
static unsigned acclen;
static char     chunk[512];          /* one TCPIPReadTCP chunk */
static char     g_line[4096];        /* one extracted \n-delimited line (must hold a
                                        full real-pool mining.notify, not just the mock's) */
static char     g_out[400];          /* outbound JSON scratch */

static StratJob g_job;               /* current parsed job (gen==0 = none) */
static int      g_havesub;           /* parsed the subscribe reply (en1 valid) */
static int      g_dev_pool;          /* 1 = pool is a private/LAN IP (mock) -> easy submit */
static unsigned long g_accepted, g_rejected;
static unsigned long g_jobs;         /* cumulative mining.notify jobs this session */

/* "a.b.c.d"? (only digits and dots) - then we can skip the DNR and parse directly */
static int is_dotted_quad(const char *s)
{
    int any = 0;
    if (!s || !*s) return 0;
    for (; *s; s++) {
        if (*s == '.') continue;
        if (*s < '0' || *s > '9') return 0;
        any = 1;
    }
    return any;
}

/* Is this host a private/LAN/loopback IPv4 literal (RFC1918 + 127/8 + 169.254/16)?
 * Real pools are public hostnames or public IPs; only the Mac mock pool (the dev
 * harness at 192.168.x / 10.x / 127.x) is private. We use this to pick the EASY dev
 * submit target there, regardless of what difficulty the mock advertises. A hostname
 * (not a dotted quad) is treated as a real, public pool. */
static int is_private_host(const char *h)
{
    unsigned long o0 = 0, o1 = 0;
    const char *p = h;
    if (!is_dotted_quad(h)) return 0;
    o0 = (unsigned long)atoi(p);
    while (*p && *p != '.') p++;
    if (*p == '.') p++;
    o1 = (unsigned long)atoi(p);
    if (o0 == 10)  return 1;                              /* 10.0.0.0/8 */
    if (o0 == 127) return 1;                              /* loopback */
    if (o0 == 192 && o1 == 168) return 1;                 /* 192.168.0.0/16 (the mock) */
    if (o0 == 172 && o1 >= 16 && o1 <= 31) return 1;      /* 172.16.0.0/12 */
    if (o0 == 169 && o1 == 254) return 1;                 /* link-local */
    return 0;
}

/* ---- dotted-quad "a.b.c.d" -> Marinetti LongWord (first octet in LOW byte) ---- */
static LongWord parse_ip(const char *s)
{
    unsigned long o[4] = {0,0,0,0};
    int idx = 0, any = 0;
    while (*s && idx < 4) {
        if (*s >= '0' && *s <= '9') { o[idx] = o[idx]*10 + (unsigned long)(*s-'0'); any = 1; }
        else if (*s == '.')         { idx++; }
        s++;
    }
    if (!any) return 0;
    return MAKE_IP(o[0] & 0xff, o[1] & 0xff, o[2] & 0xff, o[3] & 0xff);
}

static void copy_host(char *dst, const char *src)
{
    if (!src) { dst[0] = '\0'; return; }
    strncpy(dst, src, 63);
    dst[63] = '\0';
}

void strat_config(const char *ip_str,  const char *port_str,
                  const char *bip_str, const char *bport_str,
                  const char *wallet,  const char *worker)
{
    copy_host(g_host,  ip_str);
    copy_host(g_bhost, bip_str);
    g_dport = (Word)atoi(port_str);
    g_bport = bport_str ? (Word)atoi(bport_str) : 0;
    strncpy(g_wallet, wallet ? wallet : "", sizeof(g_wallet) - 1);
    g_wallet[sizeof(g_wallet) - 1] = '\0';
    strncpy(g_worker, worker ? worker : "", sizeof(g_worker) - 1);
    g_worker[sizeof(g_worker) - 1] = '\0';
    /* username = wallet.worker (solo pools key on the address); fall back to whichever
     * is present so the mock pool - which ignores the username - still authorizes. */
    if (g_wallet[0] && g_worker[0]) sprintf(g_user, "%s.%s", g_wallet, g_worker);
    else if (g_wallet[0])           strcpy(g_user, g_wallet);
    else                            strcpy(g_user, g_worker);
}

int             strat_state(void)    { return g_state; }
int             strat_on_backup(void){ return g_which == 1; }
const StratJob *strat_job(void)      { return &g_job; }
unsigned long   strat_accepted(void) { return g_accepted; }
unsigned long   strat_rejected(void) { return g_rejected; }
unsigned long   strat_jobs(void)     { return g_jobs; }
/* Dev/LAN pool (the mock) always uses the EASY target so algorithm testing keeps
 * showing accepted shares, no matter what difficulty the mock advertises. A real
 * (public) pool gates on its set_difficulty (unreachable at our hashrate -> no spam). */
int             strat_need_bits(void){ return g_dev_pool ? EASY_BITS : g_need_bits; }
int             strat_havesub(void)  { return g_havesub; }

/* hostname/port for the pool we're currently targeting */
static const char *cur_host(void)   { return g_which ? g_bhost : g_host; }
static Word        cur_port(void)   { return g_which ? g_bport : g_dport; }
static int         has_backup(void) { return g_bhost[0] != '\0'; }

/* drop the socket if we hold one open */
static void close_sock(void)
{
    if (g_open) {
        TCPIPCloseTCP(g_ipid);
        TCPIPLogout(g_ipid);
        g_open = 0;
    }
    acclen = 0;
}

void strat_stop(void)
{
    close_sock();
    g_job.gen = 0;
    g_state = STR_OFF;
}

/* a connection failure on the CURRENT pool. cause: FC_CONN (socket), FC_DNS (name),
 * FC_POOL (protocol). Retry the same pool up to POOL_MAX_RETRY, then fail over to the
 * backup (once per episode), then give up honestly to the CAUSE's terminal state. We
 * still fail over on DNS/POOL misses (the backup may work), but the terminal reason
 * matches the last cause so the user sees POOL FAIL / DNS FAIL / POOL ERR correctly. */
static void fail_conn(int cause)
{
    int max;
    unsigned long secs;
    const char *cs;
    close_sock();
    g_last_cause = cause;
    g_try++;
    g_poolfails++;
    max = has_backup() ? (POOL_MAX_RETRY * 2) : POOL_MAX_RETRY;  /* two pools earn more total */
    cs = cause == FC_DNS  ? "DNS"  :
         cause == FC_POOL ? "POOL" :
         g_conn_why == CW_REFUSED ? "CONN-REFUSED" :
         g_conn_why == CW_TIMEOUT ? "CONN-TIMEOUT" :
         g_conn_why == CW_DROPPED ? "CONN-DROPPED" : "CONN";
    mlog_event("FAIL %s try=%d/%d [%s]", cs, g_try, max, g_which ? "BAK" : "PRI");
    g_conn_why = CW_NONE;
    if (g_try >= max) {                                         /* both spent: give up */
        g_state = (cause == FC_DNS)  ? STR_DNS_FAIL  :
                  (cause == FC_POOL) ? STR_POOL_FAIL : STR_CONN_FAIL;
        mlog_event("TERM %s", cause == FC_DNS ? "DNS_FAIL" :
                              cause == FC_POOL ? "POOL_FAIL" : "CONN_FAIL");
        return;
    }
    if (g_reprobing && g_which == 0 && has_backup()) {    /* primary re-probe didn't take: */
        g_which     = 1;                                  /* hop straight back to the working backup */
        g_poolfails = 0;
        g_reprobing = 0;
        mlog_event("REPROBE miss -> BAK");
    } else if (has_backup() && g_poolfails >= FAILOVER_AFTER) {  /* eager failover to the other pool */
        g_which     = !g_which;
        g_poolfails = 0;
        mlog_event("FAILOVER -> %s", g_which ? "BAK" : "PRI");
    }
    secs    = (unsigned long)(g_poolfails < 4 ? g_poolfails + 1 : 5);  /* 1..5s, resets on flip */
    g_wait  = GetTick() + secs * TPS;
    g_state = STR_RETRY;
}

/* begin a fresh failover episode. keep_pool=1 sticks to whichever pool we were on
 * (a working pool that just dropped is most likely to come back); keep_pool=0
 * prefers the primary. */
static void new_episode(int keep_pool)
{
    if (!keep_pool) g_which = 0;
    g_try       = 0;
    g_poolfails = 0;
}

/* link-level fault (no Marinetti / no IP): report the reason + re-probe later.
 * A link outage isn't a pool attempt, so reset the whole episode (start fresh
 * from the primary once the link returns). */
static void link_fault(int st)
{
    close_sock();
    new_episode(0);
    g_state = st;
    g_wait  = GetTick() + PROBE_SECS * TPS;
    mlog_event("LINK %s", st == STR_NO_TCP ? "NO-MARINETTI" : "NO-IP");
}

/* open a socket to g_target_ip on the current pool: Login -> OpenTCP -> OPENING. */
static void open_to_ip(void)
{
    g_ipid = TCPIPLogin(userid(), g_target_ip, cur_port(), 0, 0x40);  /* TTL!=0 (M2 gotcha) */
    if (toolerror()) { fail_conn(FC_CONN); return; }
    g_open = 1;

    TCPIPOpenTCP(g_ipid);
    if (toolerror()) { fail_conn(FC_CONN); return; }

    g_syn_seen = 0;
    g_state    = STR_OPENING;
    g_deadline = GetTick() + OPEN_SECS * TPS;
}

/* attempt one connection to the current pool: link checks -> (resolve) -> open.
 * A dotted quad skips the DNR; a hostname kicks off an async lookup (STR_RESOLVING)
 * that strat_poll() finishes. A name that won't resolve is treated as that pool
 * failing (fail_conn): we retry / fail over so a bad primary name can't strand us. */
static void begin_connect(void)
{
    const char *host;
    Boolean up;
    close_sock();

    up = TCPIPGetConnectStatus();
    if (toolerror()) { link_fault(STR_NO_TCP); return; }
    if (!up)         { link_fault(STR_NO_IP);  return; }

    host = cur_host();
    g_dev_pool = is_private_host(host);         /* LAN/mock -> easy dev submit target */
    mlog_event("CONNECT %s:%u [%s]", host, (unsigned)cur_port(), g_which ? "BAK" : "PRI");
    if (is_dotted_quad(host)) {                 /* literal IP: no DNS needed */
        g_target_ip = parse_ip(host);
        open_to_ip();
        return;
    }

    g_pstr[0] = (char)strlen(host);             /* DNR wants a Pascal string */
    memcpy(g_pstr + 1, host, (unsigned char)g_pstr[0]);
    g_dnr.DNRstatus = DNR_Pending;
    TCPIPDNRNameToIP((Ref)g_pstr, (Ref)&g_dnr);
    if (toolerror()) { fail_conn(FC_DNS); return; }  /* DNR busy/unavailable -> DNS failure */
    mlog_event("DNS? %s", host);
    g_state    = STR_RESOLVING;
    g_deadline = GetTick() + DNR_SECS * TPS;
}

void strat_start(void)
{
    new_episode(0);            /* fresh start: prefer the primary */
    g_reprobe   = 0;           /* re-probe timer is armed only once mining on backup */
    g_reprobing = 0;
    g_netlogged = 0;           /* re-dump net config on the next MINING */
    g_accepted = 0;
    g_rejected = 0;
    g_jobs     = 0;
    g_job.gen  = 0;
    g_job.nbranch = 0;
    g_need_bits   = NO_SUBMIT_BITS;     /* wait for the pool's set_difficulty */
    strcpy(g_job.en2, "00000000");      /* default 4-byte extranonce2 until subscribe says */
    mine_live_reset();
    begin_connect();
}

/* ---------------- Stratum protocol (parse / send) ---------------- */

static void sendline(const char *s)
{
    TCPIPWriteTCP(g_ipid, (Ref)s, (LongWord)strlen(s), 1, 0);
}

/* integer value of the JSON "id" field, or -1 if absent/non-numeric (null).
 * Tolerates the pool's json.dumps spacing ("id": 4) and compact form ("id":4). */
static int msg_id(const char *s)
{
    const char *p = strstr(s, "\"id\"");
    if (!p) return -1;
    p += 4;
    while (*p == ' ' || *p == ':') p++;
    if (*p < '0' || *p > '9') return -1;     /* null / not a response to us */
    return atoi(p);
}

/* leading-zero BITS required for a submittable share, from a set_difficulty value.
 * p points at the number. diff < 1 (integer part 0) => easy dev target (the mock);
 * diff >= 1 => 32 + floor(log2(diff)) (real pools: effectively unreachable here). */
static int bits_for_diff(const char *p)
{
    long d;
    int bits;
    while (*p == ' ' || *p == ':' || *p == '[') p++;
    d = atol(p);
    if (d <= 0) return EASY_BITS;        /* fractional < 1 => dev/easy */
    bits = 32;
    while (d > 1) { d >>= 1; bits++; }
    return bits;
}

/* next double-quoted string from *pp into out (advances *pp past the close quote) */
static int next_str(char **pp, char *out, int outsz)
{
    char *p = strchr(*pp, '"');
    int n = 0;
    if (!p) return 0;
    p++;
    while (*p && *p != '"') { if (n < outsz - 1) out[n++] = *p; p++; }
    if (*p != '"') return 0;
    out[n] = '\0';
    *pp = p + 1;
    return 1;
}

/* dispatch one complete JSON line. Mirrors miner.c: each parse self-guards, so the
 * subscribe reply (which contains the substring "mining.notify" but no "params")
 * is never mistaken for a job. */
static void process_line(char *s)
{
    char *p;
    int   meth = (strstr(s, "\"method\"") != 0);

    /* subscribe reply: a RESPONSE (no "method") carrying [..]], extranonce1, en2_size */
    if (!g_havesub && !meth && strstr(s, "result")) {
        p = strstr(s, "]]");
        if (p) {
            p += 2;
            if (next_str(&p, g_job.en1, sizeof g_job.en1)) {
                int sz = 4, n;             /* extranonce2_size = the int after en1 */
                while (*p && (*p < '0' || *p > '9')) p++;
                if (*p >= '0' && *p <= '9') sz = atoi(p);
                if (sz < 1) sz = 1; if (sz > 18) sz = 18;
                for (n = 0; n < sz * 2; n++) g_job.en2[n] = '0';
                g_job.en2[sz * 2] = '\0';
                g_havesub = 1;
                mlog_event("SUBOK en1=%s en2sz=%d", g_job.en1, sz);
                if (g_job.gen > 0) mine_live_reset(); /* en1 arrived after notify: rebuild */
            }
        }
    }

    /* mining.set_difficulty (a NOTIFICATION - has "method"; the subscribe reply also
     * contains the substring but has no method, so it can't trip this). */
    if (meth && strstr(s, "mining.set_difficulty")) {
        p = strstr(s, "params");
        if (p && (p = strchr(p, '[')) != 0) {
            g_need_bits = bits_for_diff(p + 1);
            mlog_event("DIFF need_bits=%d", g_need_bits);
        }
    }

    /* mining.notify: a new job. Small fields go through temps; the merkle branch is
     * read straight into g_job (gen isn't bumped until the whole line parses, so viz
     * never reads a half-built job). */
    if (strstr(s, "mining.notify")) {
        p = strstr(s, "params");
        if (p && (p = strchr(p, '[')) != 0) {
            /* STATIC, not stack: these total ~660 bytes; on the 65816 a frame that
             * big in the network-poll call chain overflows the ORCA/C stack and
             * clobbers globals (was corrupting g_job's coinbase -> bogus shares the
             * pool rejects). Single-threaded cooperative loop => static is safe. */
            static char jid[32], ph[72], c1[512], c2[512], ver[16], nb[16], nt[16];
            p++;
            if (next_str(&p, jid, sizeof jid) && next_str(&p, ph, sizeof ph) &&
                next_str(&p, c1,  sizeof c1)  && next_str(&p, c2, sizeof c2)) {
                char *ob = strchr(p, '[');           /* merkle_branch array */
                int   nbr = 0;
                if (ob) {
                    char *cb = strchr(ob, ']');      /* its close bracket */
                    p = ob + 1;
                    while (nbr < STRAT_MAXBRANCH) {  /* collect hashes until ']' */
                        char *q = strchr(p, '"');
                        if (!q || (cb && q > cb)) break;
                        if (!next_str(&p, g_job.branch[nbr], 68)) break;
                        nbr++;
                    }
                    if (cb) p = cb + 1;
                }
                if (next_str(&p, ver, sizeof ver) && next_str(&p, nb, sizeof nb) &&
                    next_str(&p, nt,  sizeof nt)) {
                    strcpy(g_job.job_id, jid);   strcpy(g_job.prevhash, ph);
                    strcpy(g_job.coinb1, c1);    strcpy(g_job.coinb2,   c2);
                    strcpy(g_job.version, ver);  strcpy(g_job.nbits,    nb);
                    strcpy(g_job.ntime,  nt);    g_job.nbranch = nbr;
                    g_job.gen++;
                    g_jobs++;                    /* cumulative; survives reconnect for the JOBS readout */
                    mlog_event("JOB %s br=%d ntime=%s nbits=%s", jid, nbr, nt, nb);
                }
            }
        }
    }

    /* verdict to OUR mining.submit (id:4); id-matched so the authorize reply
     * (id:2, also "result":true) is never miscounted as an accepted share. */
    if (msg_id(s) == 4) {
        if      (strstr(s, "true"))  { g_accepted++; mlog_event("ACCEPT total=%lu", g_accepted); }
        else if (strstr(s, "false")) { g_rejected++; mlog_event("REJECT total=%lu", g_rejected); }
    }
}

/* socket -> acc[] (append; drop the buffer on overflow-without-newline to resync) */
static void pump_socket(void)
{
    TCPIPStatusTCP(g_ipid, (Ref)&sr);
    while (sr.srRcvQueued > 0) {
        Word terr = TCPIPReadTCP(g_ipid, rrBuffTypePointer, (Ref)chunk,
                                 (LongWord)sizeof(chunk), (Ref)&rr);
        if (terr || rr.rrBuffCount == 0) break;
        if (acclen + (unsigned)rr.rrBuffCount < sizeof(acc)) {
            memcpy(acc + acclen, chunk, (unsigned)rr.rrBuffCount);
            acclen += (unsigned)rr.rrBuffCount;
        } else {
            acclen = 0;   /* overflow w/o newline -> resync (avoid wedge) */
        }
        TCPIPStatusTCP(g_ipid, (Ref)&sr);
    }
}

/* pull one \n-delimited line out of acc[] into g_line; 1 if a full line was found */
static int read_line(void)
{
    unsigned i, len;
    for (i = 0; i < acclen; i++) {
        if (acc[i] == '\n') {
            len = i;
            if (len > 0 && acc[len - 1] == '\r') len--;
            if (len >= sizeof(g_line)) len = sizeof(g_line) - 1;
            memcpy(g_line, acc, len);
            g_line[len] = '\0';
            memmove(acc, acc + i + 1, acclen - (i + 1));
            acclen -= (i + 1);
            return 1;
        }
    }
    return 0;
}

static void service_lines(void)
{
    pump_socket();
    while (read_line()) process_line(g_line);
}

/* CONNECTED -> fire subscribe + authorize, arm the first-job timer -> SUBSCRIBED */
static void start_handshake(void)
{
    g_job.gen  = 0;
    g_havesub  = 0;
    mine_live_reset();                       /* force header rebuild on the new session */
    sendline("{\"id\":1,\"method\":\"mining.subscribe\",\"params\":[]}\n");
    sprintf(g_out, "{\"id\":2,\"method\":\"mining.authorize\",\"params\":[\"%s\",\"x\"]}\n",
            g_user);
    sendline(g_out);
    g_hs_deadline = GetTick() + HS_SECS * TPS;
    g_state       = STR_SUBSCRIBED;
    mlog_event("HS subscribe+authorize user=%s", g_user);
}

void strat_submit(unsigned long nonce)
{
    if (!g_open || g_state != STR_MINING) return;
    sprintf(g_out,
        "{\"id\":4,\"method\":\"mining.submit\",\"params\":[\"%s\",\"%s\",\"%s\",\"%s\",\"%08lx\"]}\n",
        g_user, g_job.job_id, g_job.en2, g_job.ntime, nonce);
    sendline(g_out);
    mlog_event("SUBMIT job=%s nonce=%08lx", g_job.job_id, nonce);
}

/* 1 if the socket is still established (else trigger the right failure path) */
static int still_up(void)
{
    TCPIPStatusTCP(g_ipid, (Ref)&sr);
    return sr.srState == TCPSESTABLISHED;
}

int strat_poll(void)
{
    LongWord t = GetTick();

    switch (g_state) {
    case STR_RESOLVING:
        TCPIPPoll();                            /* let the DNR run */
        if (g_dnr.DNRstatus == DNR_Pending) {
            if (t >= g_deadline) {              /* DNS too slow -> abort, treat as DNS fail */
                TCPIPCancelDNR((Ref)&g_dnr);
                fail_conn(FC_DNS);
            }
        } else if (g_dnr.DNRstatus == DNR_OK) {
            g_target_ip = g_dnr.DNRIPaddress;   /* name -> IP, now open the socket */
            mlog_event("DNS= %s -> %lu.%lu.%lu.%lu", cur_host(),
                       (unsigned long)(g_target_ip & 0xFF),
                       (unsigned long)((g_target_ip >> 8) & 0xFF),
                       (unsigned long)((g_target_ip >> 16) & 0xFF),
                       (unsigned long)((g_target_ip >> 24) & 0xFF));
            open_to_ip();
        } else {
            mlog_event("DNS! %s unresolved", cur_host());
            fail_conn(FC_DNS);                  /* Failed / NoDNSEntry / Cancelled */
        }
        break;

    case STR_OPENING:
        TCPIPPoll();
        TCPIPStatusTCP(g_ipid, (Ref)&sr);
        if (sr.srState == TCPSESTABLISHED) {
            mlog_event("TCP-UP %s", cur_host());
            g_state = STR_CONNECTED;            /* hand off to the handshake next step. NB: the
                                                   retry budget is NOT reset here - a TCP connect
                                                   that then fails the handshake (real pool's job
                                                   never parses) must still accrue toward failover
                                                   + a terminal POOL ERR, else it loops forever. */
        } else if (sr.srState == TCPSCLOSED && g_syn_seen) {
            g_conn_why = CW_REFUSED;
            fail_conn(FC_CONN);                 /* SYN sent then closed -> refused (fast fail) */
        } else {
            if (sr.srState != TCPSCLOSED) g_syn_seen = 1;
            if (t >= g_deadline) { g_conn_why = CW_TIMEOUT; fail_conn(FC_CONN); }  /* no response */
        }
        break;

    case STR_CONNECTED:
        TCPIPPoll();
        start_handshake();                      /* subscribe + authorize -> SUBSCRIBED */
        break;

    case STR_SUBSCRIBED:
        TCPIPPoll();
        if (!still_up()) { g_conn_why = CW_DROPPED; fail_conn(FC_CONN); break; }   /* dropped mid-handshake */
        service_lines();
        if (g_job.gen > 0) {                              /* first job in hand: a real working
                                                             session - NOW the budget is clean */
            g_try = 0; g_poolfails = 0;
            g_state = STR_MINING;
            g_reprobing = 0;                              /* this attempt concluded by landing a job */
            g_reprobe   = (g_which == 1) ? (GetTick() + REPROBE_SECS * TPS) : 0;
            mlog_event("MINING [%s] dev=%d", g_which ? "BAK" : "PRI", g_dev_pool);
            if (!g_netlogged) {             /* dump IP/DNS/link ONCE, now that Marinetti is
                                               fully up and settled (no DNR in flight) */
                mlog_netinfo();
                g_netlogged = 1;
            }
        }
        else if (t >= g_hs_deadline) fail_conn(FC_POOL);  /* pool silent / no job -> POOL ERR */
        break;

    case STR_MINING:
        TCPIPPoll();
        if (!still_up()) {                      /* a working session dropped */
            g_reprobe = 0;
            new_episode(1);                     /* sticky + fresh budget: it was working */
            g_conn_why = CW_DROPPED;
            fail_conn(FC_CONN);
            break;
        }
        if (g_reprobe && t >= g_reprobe) {      /* prefer-primary: quietly single-shot the primary.
                                                 * if it doesn't take, fail_conn hops back to backup */
            mlog_event("REPROBE -> PRI");
            g_which     = 0;
            g_poolfails = 0;
            g_reprobing = 1;
            g_reprobe   = 0;
            begin_connect();                    /* closes the backup socket, dials primary */
            break;
        }
        service_lines();                        /* new notifies bump gen; verdicts -> accepted */
        break;

    case STR_RETRY:
    case STR_NO_IP:
    case STR_NO_TCP:
        if (t >= g_wait) begin_connect();       /* re-attempt / re-probe link */
        break;

    case STR_CONN_FAIL:
    case STR_DNS_FAIL:
    case STR_POOL_FAIL:
    case STR_OFF:
    default:
        break;                                  /* terminal/idle: re-toggle LIVE to reset */
    }
    return g_state;
}
