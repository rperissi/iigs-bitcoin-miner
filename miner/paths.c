/*
 * paths.c - locate the GS Miner data directory at runtime (portable install).
 *
 * Every data file (PANEL, CONFIG, MINER.CONF/LOG/OLD) lives in <appdir>/SYSFILES/.
 * Historically the code hard-coded "/MINERAPPS/SYSFILES/...", which only resolves when
 * the *volume* is named MINERAPPS (the shipped apps disk). Copy the three parts (the
 * app, the SYSFILES folder, the Icons folder) into a folder on another volume and that
 * absolute path breaks - on real hardware the app died at launch with
 *     cannot open /MINERAPPS/SYSFILES/PANEL
 *
 * Fix: prefer a path relative to GS/OS prefix 1. When GS/OS launches an application it
 * sets prefix 1 to the directory the app was loaded from, so "1/SYSFILES/PANEL" finds
 * SYSFILES sitting next to the app in ANY folder, on ANY volume. We keep the legacy
 * absolute path as a fallback so the original /MINERAPPS disk (and any launch that does
 * not set prefix 1 the way we expect) still works.
 *
 * The base is probed once at paths_init() - we try each candidate's PANEL (which always
 * ships in SYSFILES) and cache the first that opens. miner_sysfile() then just joins the
 * cached base with the leaf name. It returns one of a small ring of static buffers so a
 * caller needing two live paths at once (e.g. rename(LOG, OLD)) is safe; the app is a
 * single cooperative loop, so there is no re-entrancy to worry about.
 *
 * Build: paths.c is part of the viz link line, e.g.
 *   occ -b -O255 -w255 viz.c mine.c numfmt.c stratum.c cfg.c mlog.c paths.c -L. -llib65816hash -o viz
 */
#include <stdio.h>
#include <string.h>
#include "paths.h"

static const char *const BASES[] = {
    "1/SYSFILES/",            /* app-relative: prefix 1 = launch dir -> copy-anywhere */
    "/GSMINER/SYSFILES/",     /* shipped volume name (current disks)                  */
    "/MINERAPPS/SYSFILES/"    /* legacy volume name (older disks)                     */
};
#define NBASE 3

static const char *g_base = 0;      /* resolved data-dir base (always ends in '/') */
static char        g_ring[4][80];   /* >=2 live results so rename(a,b) stays valid  */
static int         g_ri = 0;

void paths_init(void)
{
    int   i;
    char  probe[80];
    FILE *f;

    for (i = 0; i < NBASE; i++) {
        strcpy(probe, BASES[i]);
        strcat(probe, "PANEL");          /* PANEL always lives in SYSFILES -> good probe */
        f = fopen(probe, "rb");
        if (f) { fclose(f); g_base = BASES[i]; return; }
    }
    g_base = BASES[0];                   /* none opened: keep portable form for the error */
}

const char *miner_sysfile(const char *name)
{
    char *b;

    if (!g_base) g_base = BASES[0];      /* defensive: called before paths_init() */
    b = g_ring[g_ri];
    g_ri = (g_ri + 1) & 3;
    strcpy(b, g_base);
    strcat(b, name);
    return b;
}
