/* GNUPLOT - tabulate.h */

#ifndef GNUPLOT_TABULATE_H
# define GNUPLOT_TABULATE_H

#include "syscfg.h"

/* Routines in tabulate.c needed by other modules: */

void print_table(struct curve_points * first_plot, int plot_num);
void print_3dtable(int pcount);
TBOOLEAN tabulate_one_line(struct curve_points *plot, double v[], struct value str[], int ncols);

extern FILE *table_outfile;
extern udvt_entry *table_var;
extern TBOOLEAN table_mode;
extern char *table_sep;

#endif /* GNUPLOT_TABULATE_H */
