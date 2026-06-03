/*
 * numfmt.c - readout number formatting, split out of viz.c for modularity. Built with
 * the large memory model (occ -b) so the project's code can span multiple 64KB banks.
 * Pure functions; no shared state. See numfmt.h.
 */
#include <stdio.h>
#include <string.h>
#include "numfmt.h"

void fmt_compact(char *buf, unsigned long v)
{
    if (v < 1000UL)            sprintf(buf, "%lu", v);
    else if (v < 1000000UL)    sprintf(buf, "%lu.%luK", v / 1000UL, (v % 1000UL) / 100UL);
    else if (v < 1000000000UL) sprintf(buf, "%lu.%luM", v / 1000000UL, (v % 1000000UL) / 100000UL);
    else                       sprintf(buf, "%lu.%luG", v / 1000000000UL, (v % 1000000000UL) / 100000000UL);
}

/* SI-suffixed value for the difficulty well: "88.1 T", "1.2 P". One decimal, a
 * space before the letter to match the static "88.1 T" the dashboard shipped with. */
void fmt_si(char *buf, double v)
{
    static const char *S[] = { "", "K", "M", "G", "T", "P", "E", "Z" };
    int k = 0, md;
    if (v < 0.0) v = 0.0;
    while (v >= 1000.0 && k < 7) { v /= 1000.0; k++; }
    md = (int)(v * 10.0 + 0.5);
    if (md >= 10000) { md = 1000; k++; }       /* rounding tipped to 1000.0 -> next unit */
    if (k == 0) sprintf(buf, "%d", md / 10);
    else        sprintf(buf, "%d.%d %s", md / 10, md % 10, S[k]);
}

/* mantissa.dEexp for astronomically large doubles */
void fmt_exp(char *buf, double v)
{
    int e = 0, md;
    if (v < 1.0) { strcpy(buf, "0"); return; }
    while (v >= 10.0) { v /= 10.0; e++; }
    md = (int)(v * 10.0 + 0.5);
    if (md >= 100) { md = 10; e++; }
    sprintf(buf, "%d.%dE%d", md / 10, md % 10, e);
}

void fmt_years(char *buf, double y)
{
    /* field is ~8 chars wide: keep the magnitude letter tight to the number so
     * "YRS" always fits (no more clipped "749 T YR"). T=trillion, Q=quadrillion. */
    if (y < 1e3)       sprintf(buf, "%d YRS",  (int)(y + 0.5));
    else if (y < 1e6)  sprintf(buf, "%dK YRS", (int)(y / 1e3 + 0.5));
    else if (y < 1e9)  sprintf(buf, "%dM YRS", (int)(y / 1e6 + 0.5));
    else if (y < 1e12) sprintf(buf, "%dB YRS", (int)(y / 1e9 + 0.5));
    else if (y < 1e15) sprintf(buf, "%dT YRS", (int)(y / 1e12 + 0.5));
    else if (y < 1e18) sprintf(buf, "%dQ YRS", (int)(y / 1e15 + 0.5));
    else { char e[20]; fmt_exp(e, y); sprintf(buf, "%sY", e); }
}

/* spelled-out years for the wide "BLOCK ETA" well: "1.3 TRILLION YRS",
 * "742 QUADRILLION YRS". One decimal only for a single-digit mantissa (keeps the
 * "1.3 QUADRILLION" look lively as the hashrate wobbles); past decillion fall back
 * to exponent. The widened 145px well fits "BLOCK ETA" + the 11-letter words. */
void fmt_years_words(char *buf, double y)
{
    static const char *W[] = { "", "THOUSAND", "MILLION", "BILLION", "TRILLION",
        "QUADRILLION", "QUINTILLION", "SEXTILLION", "SEPTILLION", "OCTILLION",
        "NONILLION", "DECILLION" };
    double m = y;
    int k = 0, md;
    if (y < 1.0) { strcpy(buf, "0 YRS"); return; }
    while (m >= 1000.0 && k < 11) { m /= 1000.0; k++; }
    if (k == 0)                  { sprintf(buf, "%d YRS", (int)(m + 0.5)); return; }
    if (k >= 11 && m >= 1000.0)  { char e[20]; fmt_exp(e, y);              /* off the chart */
                                   sprintf(buf, "%s YRS", e); return; }
    if (m < 10.0) { md = (int)(m * 10.0 + 0.5); sprintf(buf, "%d.%d %s YRS", md/10, md%10, W[k]); }
    else          { sprintf(buf, "%d %s YRS", (int)(m + 0.5), W[k]); }
}

void fmt_uptime(char *buf, unsigned long s)
{
    unsigned long h = s / 3600UL, m = (s / 60UL) % 60UL, ss = s % 60UL;
    sprintf(buf, "%02lu:%02lu:%02lu", h, m, ss);
}
