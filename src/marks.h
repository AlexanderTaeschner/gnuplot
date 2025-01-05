#ifndef MARKS_H
#define MARKS_H

#include "syscfg.h"
#include "gp_types.h"
#include "term_api.h"

/* function prototypes */

struct mark_data *get_mark(struct mark_data *first, int tag);
void set_mark(void);
void free_mark(struct mark_data *mark);
void save_marks(FILE *fp);
void delete_mark(struct mark_data *prev, struct mark_data *this);
struct mark_data *push_mark(struct mark_data *first, struct mark_data *mark);
void clear_mark(void);
void unset_mark(void);
void show_one_mark(struct mark_data *mark);
void show_mark(void);

void do_mark (struct mark_data *mark,
         double x, double y, double xscale, double yscale, double angle, 
         int units, 
         TBOOLEAN clip,
         struct fill_style_type *fill_properties,
         struct lp_style_type *lp_properties, 
         struct curve_points *plot, double varcolor,
         int max_vertices, gpiPoint *vertex, gpiPoint *fillarea);

/* exported variables */

extern struct mark_data *first_mark;

/* Data structure for 'set mark' */
struct mark_data {
    struct mark_data *next;
    int tag;
    double xmin, xmax;
    double ymin, ymax;
    int vertices;
    int asize;		/* allocated size of polygon.vertex array */
    t_polygon polygon;
    char *title;	/* will be reported by "show mark" */
    struct fill_style_type mark_fillstyle;
    struct t_colorspec mark_fillcolor;
};

#define MARK_MAX_VERTICES 1024	/* admittedly arbitrary */

/* mode parameter in mark polygon vertices */
#define MARKS_FILLSTYLE 	0
#define MARKS_STROKE		1
#define MARKS_FILL		2
#define MARKS_FILL_STROKE	3
#define MARKS_FILL_BACKGROUND	4

#endif /* MARKS_H */
