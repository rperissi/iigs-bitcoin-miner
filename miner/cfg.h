/*
 * cfg.h - interface to the CONFIG subsystem (cfg.c). The config logic (persistence,
 * validation, Marinetti probe, runtime apply) was split out of viz.c for modularity.
 * Built with the large memory model (occ -b) like the rest of the project. Keep these
 * extern decls in step with the (now non-static) definitions in viz.c.
 */
#ifndef __CFG_H__
#define __CFG_H__

/* MINER.CONF path is resolved at runtime via miner_sysfile("MINER.CONF") - see paths.c */
#define CF_NF       7                 /* MODE, WORKER, WALLET, POOL, PORT, BACKUP, BPORT */

#define NET_NONE    0                 /* Marinetti tool set $36 not loaded */
#define NET_NO_LINK 1                 /* stack up, TCPIPGetConnectStatus = false */
#define NET_UP      2                 /* stack up and IP link connected */

#define TCP_OFF     0                 /* dark: stack not loaded */
#define TCP_DOWN    1                 /* red: Marinetti up, not connected */
#define TCP_UP      2                 /* green: TCP link up */
#define TCP_ACT     3                 /* green + flash: TX/RX activity */

/* config form state - defined (non-static) in viz.c, shared with cfg.c */
extern char cfg_mode[8];
extern char cfg_worker[20];
extern char cfg_wallet[64];
extern char cfg_pool[32];
extern char cfg_pport[8];
extern char cfg_back[32];
extern char cfg_bport[8];
extern char * const cf_buf[CF_NF];
extern const int    cf_max[CF_NF];

/* runtime lamp/run state - defined (non-static) in viz.c */
extern int g_tcp;
extern int g_running;
extern int g_strat_last;

/* config logic (cfg.c) called from viz.c's root code */
void        cfg_net_bringup(void);            /* load + start Marinetti, log tool errors */
int         cf_mode_token(const char *s);
int         cf_net_state(void);
const char *cf_live_block_msg(void);
int         strat_to_tcp(int st);
void        cfg_sync_demo(void);
void        cfg_apply_runtime(void);
void        cfg_load_file(void);
int         cfg_save_file(void);
int         cf_valid_host(const char *s);
int         cf_valid_port(const char *s);
int         cf_all_digit_dot(const char *s);
const char *cf_valid_wallet(void);
const char *cfg_validate(int *bad);

#endif /* __CFG_H__ */
