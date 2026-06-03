/*
 * numfmt.h - number/string formatting helpers for the dashboard readouts. Split out
 * of viz.c for modularity (project built with the large memory model, occ -b).
 * These are pure functions (no shared state).
 */
#ifndef __NUMFMT_H__
#define __NUMFMT_H__

void fmt_compact(char *buf, unsigned long v);       /* 12.3K / 4.5M / 1.2G */
void fmt_si(char *buf, double v);                   /* "88.1 T" / "1.2 P" (SI, 1 decimal) */
void fmt_exp(char *buf, double v);                  /* mantissa.dEexp for huge doubles */
void fmt_years(char *buf, double y);                /* "742T YRS" (tight, ~8 chars) */
void fmt_years_words(char *buf, double y);          /* "1.3 TRILLION YRS" (wide well) */
void fmt_uptime(char *buf, unsigned long s);        /* HH:MM:SS */

#endif /* __NUMFMT_H__ */
