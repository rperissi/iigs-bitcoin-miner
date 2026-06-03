/*
 * paths.h - runtime resolution of the GS Miner data directory.
 *
 * All on-disk data (PANEL, CONFIG, MINER.CONF/LOG/OLD) lives in <appdir>/SYSFILES/.
 * Call paths_init() ONCE at startup (before any data file is opened); afterwards
 * miner_sysfile("PANEL") etc. returns a full pathname to that file. See paths.c for
 * why this beats the old hard-coded /GSMINER/SYSFILES/... absolute paths.
 */
#ifndef PATHS_H
#define PATHS_H

void        paths_init(void);
const char *miner_sysfile(const char *name);

#endif
