/*
 * mlog.c - on-disk diagnostic log (see mlog.h for the design + rotation policy).
 *
 * stdio only (fopen/fputs/fclose + remove/rename), mirroring cfg.c's file I/O. Each
 * event opens/appends/closes (write-through) so the log survives a hang. The line
 * scratch is STATIC, not stack: mlog_event() runs in the network-poll call chain on
 * the 65816, where a big frame would overflow the ORCA/C stack (same hazard we hit in
 * stratum.c / mine.c). Single-threaded cooperative loop => static is safe.
 *
 * Build: occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c paths.c -L. -llib65816hash -o viz
 */
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <types.h>
#include <orca.h>
#include <misctool.h>
#include "tcpip.h"
#include "mlog.h"
#include "paths.h"

/* MINER.LOG / MINER.OLD live in <appdir>/SYSFILES/ - resolved at runtime via
 * miner_sysfile() (see paths.c) so the log follows the app to any folder. */
#define LOG_CAP  8192UL          /* per-file hard cap; rotate at this size */
#define TPS_L    60UL            /* ticks per second */

static LongWord      g_t0;       /* session start tick, for the T+ stamp */
static unsigned long g_bytes;    /* bytes written to the current MINER.LOG */
static int           g_armed;    /* 1 once mlog_open() has run */
static char          g_lbuf[200];/* STATIC line scratch (off the 65816 stack) */

/* MINER.LOG -> MINER.OLD (overwrite prior), fresh MINER.LOG. Missing files are fine. */
static void rotate(void)
{
    remove(miner_sysfile("MINER.OLD"));
    rename(miner_sysfile("MINER.LOG"), miner_sysfile("MINER.OLD"));
    g_bytes = 0;
}

/* "T+ssss.hh " from ticks since session start (hundredths of a second). */
static int stamp(char *out)
{
    LongWord dt   = GetTick() - g_t0;
    unsigned long s  = (unsigned long)(dt / TPS_L);
    unsigned long hh = (unsigned long)((dt % TPS_L) * 100UL / TPS_L);
    return sprintf(out, "T+%lu.%02lu ", s, hh);
}

void mlog_open(void)
{
    rotate();                    /* push last run to MINER.OLD, start fresh */
    g_t0    = GetTick();
    g_bytes = 0;
    g_armed = 1;
}

void mlog_event(const char *fmt, ...)
{
    va_list ap;
    FILE   *f;
    int     n;
    if (!g_armed) return;

    n = stamp(g_lbuf);
    va_start(ap, fmt);
    n += vsprintf(g_lbuf + n, fmt, ap);
    va_end(ap);
    g_lbuf[n++] = '\n';
    g_lbuf[n]   = '\0';

    if (g_bytes + (unsigned long)n > LOG_CAP) rotate();
    f = fopen(miner_sysfile("MINER.LOG"), "a");
    if (!f) return;
    fputs(g_lbuf, f);
    fclose(f);
    g_bytes += (unsigned long)n;
}

/* Marinetti IP Long -> dotted quad. Byte order matches MAKE_IP()/DNRIPaddress that
 * TCPIPLogin accepts: octet1 is the LOW byte. */
static void ipstr(char *out, LongWord ip)
{
    sprintf(out, "%lu.%lu.%lu.%lu",
            (unsigned long)(ip & 0xFF), (unsigned long)((ip >> 8) & 0xFF),
            (unsigned long)((ip >> 16) & 0xFF), (unsigned long)((ip >> 24) & 0xFF));
}

void mlog_netinfo(void)
{
    LongWord ip;
    Boolean  up;
    char     a[24];

    if (!g_armed) return;

    /* ONLY the two calls proven safe in production (v78 used both): TCPIPGetMyIPAddress
     * and TCPIPGetConnectStatus. The richer getters (TCPIPGetDNS / GetConnectionMethod /
     * GetMTU) returned garbage on the GS (dns came back 0x88888888 = uninit fill) and
     * calling them on the live established socket froze the session - so they're dropped
     * until validated. Subnet mask / gateway / MAC aren't exposed by Marinetti anyway. */
    ip = TCPIPGetMyIPAddress();
    if (toolerror()) { mlog_event("NET tcpip-unavailable (DEMO/no Marinetti)"); return; }
    up = TCPIPGetConnectStatus();
    ipstr(a, ip);
    mlog_event("NET ip=%s up=%s", a, up ? "Y" : "N");
}

void mlog_stat(unsigned long hps, int bestbits,
               unsigned long acc, unsigned long rej,
               int on_backup, unsigned long up_secs)
{
    mlog_event("STAT hr=%lu best=%d acc=%lu rej=%lu pool=%s up=%lus",
               hps, bestbits, acc, rej, on_backup ? "BAK" : "PRI", up_secs);
}

void mlog_close(void)
{
    if (!g_armed) return;
    mlog_event("BYE");
    g_armed = 0;
}
