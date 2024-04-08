/*
 * This program creates some binary data files used by the demo
 * binary.dem to exercise gnuplot's binary input routines.
 * This code is not used by gnuplot itself.
 *
 * Copyright (c) 1992 Robert K. Cunningham, MIT Lincoln Laboratory
 *
 */

/*
 * Ethan A Merritt July 2014
 * Remove dependence on any gnuplot source files
 */
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>
#include <math.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE_STDLIB_H
#  include <stdlib.h>
#else
#  ifdef HAVE_MALLOC_H
#    include <malloc.h>
#  endif
#endif

static float function (int p, double x, double y);

typedef struct {
  float xmin, xmax;
  float ymin, ymax;
} range;

#define NUM_PLOTS 2
static range TheRange[] = {{-3,3,-2,2},
			   {-3,3,-3,3},
			   {-3,3,-3,3}}; /* Sampling rate causes this to go from -3:6*/

static float
function(int p, double x, double y)
{
    float t = 0;			/* HBB 990828: initialize */

    switch (p) {
    case 0:
	t = 1.0 / (x * x + y * y + 1.0);
	break;
    case 1:
	t = sin(x * x + y * y) / (x * x + y * y);
	if (t > 1.0)
	    t = 1.0;
	break;
    case 2:
	t = sin(x * x + y * y) / (x * x + y * y);
	/* sinc modulated sinc */
	t *= sin(4. * (x * x + y * y)) / (4. * (x * x + y * y));
	if (t > 1.0)
	    t = 1.0;
	break;
    default:
	fprintf(stderr, "Unknown function\n");
	break;
    }
    return t;
}


int
fwrite_matrix( FILE *fout, float **m, int xsize, int ysize, float *rt, float *ct)
{
    int j;
    int status;
    float length = ysize;

    if ((status = fwrite((char *) &length, sizeof(float), 1, fout)) != 1) {
	fprintf(stderr, "fwrite 1 returned %d\n", status);
	return (0);
    }
    fwrite((char *) ct, sizeof(float), ysize, fout);
    for (j = 0; j < xsize; j++) {
	fwrite((char *) &rt[j], sizeof(float), 1, fout);
	fwrite((char *) (m[j]), sizeof(float), ysize, fout);
    }

    return (1);
}


#define ISOSAMPLES 5.0

int
main(void)
{
    int plot;
    int i, j;
    int im;
    float x, y;
    float *rt, *ct;
    float **m;
    int xsize, ysize;
    char buf[256];
    FILE *fout;

    uint32_t i32;	/* we specifically want to test mixing four-byte and eight-byte values */
    double v[3];
    char inbuf[120];
    FILE *fin;

    /*  Create a few standard test interfaces */

    for (plot = 0; plot < NUM_PLOTS; plot++) {
	xsize = (TheRange[plot].xmax - TheRange[plot].xmin) * ISOSAMPLES + 1;
	ysize = (TheRange[plot].ymax - TheRange[plot].ymin) * ISOSAMPLES + 1;

	sprintf(buf, "binary%d", plot + 1);
	if (!(fout = fopen(buf, "wb"))) {
	    fprintf(stderr, "Could not open output file\n");
	    return EXIT_FAILURE;
	}

	rt = calloc(xsize, sizeof(float));
	ct = calloc(ysize, sizeof(float));
	m = calloc(xsize, sizeof(m[0]));
	for (im = 0; im < xsize; im++) {
		m[im] = calloc(ysize, sizeof(m[0][0]));
	}

	for (y = TheRange[plot].ymin, j = 0; j < ysize; j++, y += 1.0 / (double) ISOSAMPLES) {
	    ct[j] = y;
	}

	for (x = TheRange[plot].xmin, i = 0; i < xsize; i++, x += 1.0 / (double) ISOSAMPLES) {
	    rt[i] = x;
	    for (y = TheRange[plot].ymin, j = 0; j < ysize; j++, y += 1.0 / (double) ISOSAMPLES) {
		m[i][j] = function(plot, x, y);
	    }
	}

	fwrite_matrix(fout, m, xsize, ysize, rt, ct);

	free(rt);
	free(ct);
	for (im = 0; im < xsize; im++)
	    free(m[im]);
	free(m);
    }

    /* Show that it's ok to vary sampling rate, as long as x1<x2, y1<y2... */

    sprintf(buf, "binary%d", plot + 1);
    if (!(fout = fopen(buf, "wb"))) {
	fprintf(stderr, "Could not open output file\n");
	return EXIT_FAILURE;
    }

    xsize = (TheRange[plot].xmax - TheRange[plot].xmin) * ISOSAMPLES + 1;
    ysize = (TheRange[plot].ymax - TheRange[plot].ymin) * ISOSAMPLES + 1;

    rt = calloc(xsize, sizeof(float));
    ct = calloc(ysize, sizeof(float));
    m = calloc(xsize, sizeof(m[0]));
    for (im = 0; im < xsize; im++) {
	    m[im] = calloc(ysize, sizeof(m[0][0]));
    }

    for (y = TheRange[plot].ymin, j = 0; j < ysize; j++, y += 1.0 / (double) ISOSAMPLES) {
	ct[j] = y > 0 ? 2 * y : y;
    }
    for (x = TheRange[plot].xmin, i = 0; i < xsize; i++, x += 1.0 / (double) ISOSAMPLES) {
	rt[i] = x > 0 ? 2 * x : x;
	for (y = TheRange[plot].ymin, j = 0; j < ysize; j++, y += 1.0 / (double) ISOSAMPLES) {
	    m[i][j] = function(plot, x, y);
	}
    }

    fwrite_matrix(fout, m, xsize, ysize, rt, ct);

    free(rt);
    free(ct);
    for (im = 0; im < xsize; im++)
	free(m[im]);
    free(m);

    /* Convert polygon data to binary form.
     * Blank lines are indicated by placing NaN in v[0].
     * The binary file can be read as 'binary format="%3float64%int32" blank=NaN'
     */
    if (!(fin = fopen("dodecahedron.dat", "r"))) {
	fprintf(stderr, "Could not open polygon input file\n");
	return EXIT_FAILURE;
    }
    if (!(fout = fopen("dodecahedron.bin", "wb"))) {
	fprintf(stderr, "Could not open output file\n");
	return EXIT_FAILURE;
    }

    i32 = 0;
    while (fgets( inbuf, sizeof(inbuf), fin )) {
	if (inbuf[0] == '#')
	    continue;
	if (sscanf(inbuf, "%lf%lf%lf", &v[0], &v[1], &v[2]) < 3) {
	    v[0] = NAN;
	    i32++;
	}
	fwrite( v, sizeof(double), 3, fout );
	fwrite( &i32, sizeof(i32), 1, fout );
    }

    fclose(fin);
    fclose(fout);

    return 0;
}
