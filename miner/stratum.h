/*
 * stratum.h - non-blocking Marinetti/Stratum client (shared by miner.c + viz.c).
 *
 * The headless miner.c proved the protocol with a BLOCKING flow (spin until
 * ESTABLISHED, spin until notify, grind). The dashboard (viz.c) is an event loop
 * that must keep the cursor + SHR panel alive, so the same protocol is exposed
 * here as a COOPERATIVE STATE MACHINE: call strat_poll() once per main-loop pass
 * and it advances one step (and services TCPIPPoll) without ever blocking.
 *
 * M8a scope: connect only (login -> open -> ESTABLISHED), bounded inbound buffer
 * with overflow resync, and the 6-retry / escalating-backoff drop policy. The
 * subscribe/authorize/notify/submit layers (M8b/M8c) extend the state enum below.
 */
#ifndef __STRATUM_H__
#define __STRATUM_H__

#ifndef __TYPES__
#include <types.h>
#endif

/* Connection/protocol state. Also drives the dashboard badge + TCP lamp.
 * Order matters only for readability; treat as opaque tags. */
enum {
    STR_OFF = 0,     /* not started (demo / idle) */
    STR_NO_TCP,      /* Marinetti tool set ($36) not loaded */
    STR_NO_IP,       /* stack up, no IP link (GetConnectStatus == false) */
    STR_RESOLVING,   /* DNR lookup of a pool hostname in flight */
    STR_OPENING,     /* TCPIPLogin/OpenTCP issued, awaiting ESTABLISHED */
    STR_CONNECTED,   /* TCP established; about to subscribe (transient) */
    STR_SUBSCRIBED,  /* mining.subscribe + authorize sent; awaiting first job */
    STR_MINING,      /* job in hand: dashboard mines the pool's work (M8b target) */
    STR_RETRY,       /* dropped/failed; backing off before re-attempt */
    STR_CONN_FAIL,   /* TCP connect/reconnect retries exhausted (no socket) */
    STR_DNS_FAIL,    /* retries exhausted and the last failure was name resolution */
    STR_POOL_FAIL    /* connected but the pool misbehaves: no job / auth reject / garbage */
};

/* Parsed Stratum job (filled from mining.notify). viz reads this to build the
 * 80-byte header. gen bumps on each new job (0 = none yet); en1 = extranonce1
 * from the subscribe reply, en2 = our extranonce2 (zeros, sized to the pool's
 * extranonce2_size). branch[] = the merkle branch (empty against the mock pool;
 * non-empty on a real pool, folded into the merkle root by viz). Buffers are
 * sized for real pools (longer coinbases + a full branch), not just the mock. */
#define STRAT_MAXBRANCH 16       /* merkle-branch depth (>= log2 of any real block) */

typedef struct {
    unsigned long gen;
    char job_id[32];
    char prevhash[72];
    char coinb1[512];
    char coinb2[512];
    char en1[24];
    char en2[40];
    char version[16];
    char nbits[16];
    char ntime[16];
    int  nbranch;
    char branch[STRAT_MAXBRANCH][68];
} StratJob;

#define POOL_MAX_RETRY 8         /* per-pool connect attempts; small pools flake a lot */

/* Point the client at the pools from the CONFIG fields. Each ip = dotted quad
 * ("192.168.2.1") OR hostname; port = decimal ("3333"). The backup is optional:
 * pass "" / "0" (or an empty host) and failover is disabled. The Stratum username
 * is composed as wallet[.worker] (real solo pools key on the BTC address; the
 * worker label distinguishes rigs). The mock pool ignores the username. */
void strat_config(const char *ip_str,  const char *port_str,
                  const char *bip_str, const char *bport_str,
                  const char *wallet,  const char *worker);

/* 1 once the client is using/targeting the BACKUP pool (else primary). */
int  strat_on_backup(void);

/* Begin (or restart) a connection attempt; resets the retry budget. */
void strat_start(void);

/* Close the socket cleanly (CloseTCP + Logout). Safe to call in any state. */
void strat_stop(void);

/* Advance the state machine one step. Call once per main-loop pass while LIVE.
 * Services TCPIPPoll() internally and never blocks. Returns the current STR_*. */
int  strat_poll(void);

/* Current state without advancing. */
int  strat_state(void);

/* M8b protocol surface (valid once STR_MINING). strat_job() returns the live job
 * (gen==0 until the first notify). strat_submit() sends mining.submit for a winning
 * nonce against the current job; the pool's verdict feeds strat_accepted(). */
const StratJob *strat_job(void);
void           strat_submit(unsigned long nonce);
unsigned long  strat_accepted(void);
unsigned long  strat_rejected(void);
/* Cumulative work units (mining.notify jobs) the pool has pushed this session.
 * Unlike StratJob.gen (which the handshake zeroes on every reconnect), this only
 * resets on strat_start(), so the dashboard's live JOBS readout survives failover. */
unsigned long  strat_jobs(void);

/* Leading-zero BITS a hash must have for us to submit it, derived from the pool's
 * mining.set_difficulty (diff < 1 => easy dev target; diff >= 1 => 32 + log2(diff)).
 * A private/LAN pool (the mock at 192.168.x/10.x/127.x) always returns the EASY dev
 * target so shares keep flowing for algorithm testing. A real (public) pool gates on
 * its set_difficulty, which is unreachable at our hashrate -> no submits, no spam. */
int            strat_need_bits(void);
/* 1 once the subscribe reply has been parsed (extranonce1 valid). */
int            strat_havesub(void);

#endif /* __STRATUM_H__ */
