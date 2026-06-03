/*
 * mlog.h - tiny on-disk diagnostic log for the GS miner.
 *
 * Two capped files on the boot volume (rotation, never unbounded growth):
 *   /GSMINER/SYSFILES/MINER.LOG  - the current session (being filled)
 *   /GSMINER/SYSFILES/MINER.OLD  - the previous full block
 * Each is hard-capped at LOG_CAP bytes; when MINER.LOG fills (or on launch) it
 * rotates to MINER.OLD and a fresh MINER.LOG starts. So you always have the most
 * recent ~16KB of activity (current + prior) on a tiny floppy, and never lose more
 * than the oldest block.
 *
 * Writes are EVENT-DRIVEN (state changes, faults, a periodic STAT heartbeat) - NEVER
 * inside the hash loop - and each line is opened/appended/closed (write-through) so a
 * hang or reset still leaves the breadcrumb that explains it on disk.
 *
 * Built with the large memory model (occ -b) like the rest of the project.
 */
#ifndef __MLOG_H__
#define __MLOG_H__

/* Arm logging: rotate (push the last run to MINER.OLD), start a fresh MINER.LOG,
 * and reset the T+ session clock. Call once at startup, before the first event. */
void mlog_open(void);

/* Append one printf-style line, prefixed with a "T+ssss.hh " session timestamp and
 * newline-terminated. Auto-rotates when the cap is hit. No-op until mlog_open(). */
void mlog_event(const char *fmt, ...);

/* Dump the live Marinetti network config (IP / DNS1 / DNS2 / connect method / MTU /
 * link status) as NET lines. Safe to call when TCP/IP isn't available (notes that). */
void mlog_netinfo(void);

/* The periodic heartbeat line: hashrate, best bits, accepted/rejected, pool, uptime. */
void mlog_stat(unsigned long hps, int bestbits,
               unsigned long acc, unsigned long rej,
               int on_backup, unsigned long up_secs);

/* Final line for a clean exit. */
void mlog_close(void);

#endif /* __MLOG_H__ */
