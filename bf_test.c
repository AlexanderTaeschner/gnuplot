#ifndef lint
static char *RCSid = "$Id: bf_test.c%v 3.50 1993/07/09 05:35:24 woo Exp $";
#endif


/*
 * Test routines for binary files
 * cc bf_test.c -o bf_test binary_files.o -lm
 *
 * Copyright (c) 1992 Robert K. Cunningham, MIT Lincoln Laboratory
 *
 */
#include <stdio.h>
#if !defined(apollo) && !defined(sequent) && !defined(u3b2) && !defined(alliant) && !defined(sun386)
#include <stdlib.h>
#else
extern char *malloc();
#endif
#include <math.h>
float *vector();
float *extend_vector();
float *retract_vector();
float **matrix();
float **extend_matrix();
float **retract_matrix();
void free_matrix();
void free_vector();

typedef struct {
  float xmin, xmax;
  float ymin, ymax;
} range;

#define NUM_PLOTS 2
range TheRange[] = {{-3,3,-2,2},
                    {-3,3,-3,3},
                    {-3,3,-3,3}};/* Sampling rate causes this to go from -3:6*/

/*---- Stubs to make this work without including huge libraries ----*/
void int_error(error_text,dummy)
     char *error_text;
     int dummy;
{
  fprintf(stderr,"Fatal error..\n");
  fprintf(stderr,"%s\n",error_text);
  fprintf(stderr,"...now exiting to system ...\n");
  exit(1);
}

char *alloc(i)
unsigned long i;
{
  return(malloc(i));
}
/*---- End of stubs ----*/

float function(p,x,y)
int p;
double x,y;
{
  float t;
  switch (p){
  case 0:
    t = 1.0/(x*x+y*y+1.0);
    break;
  case 1:
    t = sin(x*x + y*y)/(x*x + y*y);
    if (t> 1.0)  t = 1.0;
    break;
  case 2:
    t = sin(x*x + y*y)/(x*x + y*y);
    t *= sin(4.*(x*x + y*y))/(4.*(x*x + y*y));  /* sinc modulated sinc */
    if (t> 1.0)  t = 1.0;
    break;
  default:
    fprintf(stderr, "Unknown function\n");
    break;
  }
  return t;
}

#define ISOSAMPLES (double)5
int
main()
{
  int plot;
  int i,j;
  float x,y;
  float *rt,*ct;
  float **m;
  int xsize,ysize;
  char buf[256];
  FILE *fout;
/*  Create a few standard test interfaces */

  for(plot = 0; plot < NUM_PLOTS; plot++){
    xsize = (TheRange[plot].xmax - TheRange[plot].xmin)*ISOSAMPLES +1;
    ysize = (TheRange[plot].ymax - TheRange[plot].ymin)*ISOSAMPLES +1;

    rt = vector(0,xsize-1);
    ct = vector(0,ysize-1);
    m =  matrix(0,xsize-1,0,ysize-1);

    for(y=TheRange[plot].ymin,j=0;j<ysize;j++,y+=1.0/(double)ISOSAMPLES){
      ct[j] = y;
    }

    for(x=TheRange[plot].xmin,i=0;i<xsize;i++,x+=1.0/(double)ISOSAMPLES){
      rt[i] = x;
      for(y=TheRange[plot].ymin,j=0;j<ysize;j++,y+=1.0/(double)ISOSAMPLES){
        m[i][j] = function(plot,x,y);
      }
    }

    sprintf(buf,"binary%d",plot+1);
#ifdef UNIX
    if(!(fout = fopen(buf,"w")))
#else
    if(!(fout = fopen(buf,"wb")))
#endif
      int_error("Could not open file",0);
    else{
      fwrite_matrix(fout,m,0,xsize-1,0,ysize-1,rt,ct);
    }
    free_vector(rt,0,ysize-1);
    free_vector(ct,0,xsize-1);
    free_matrix(m,0,xsize-1,0,ysize-1);
  }

  /* Show that it's ok to vary sampling rate, as long as x1<x2, y1<y2... */
  xsize = (TheRange[plot].xmax - TheRange[plot].xmin)*ISOSAMPLES +1;
  ysize = (TheRange[plot].ymax - TheRange[plot].ymin)*ISOSAMPLES +1;

  rt = vector(0,xsize-1);
  ct = vector(0,ysize-1);
  m =  matrix(0,xsize-1,0,ysize-1);

  for(y=TheRange[plot].ymin,j=0;j<ysize;j++,y+=1.0/(double)ISOSAMPLES){
    ct[j] = y>0?2*y:y;
  }
  for(x=TheRange[plot].xmin,i=0;i<xsize;i++,x+=1.0/(double)ISOSAMPLES){
    rt[i] = x>0?2*x:x;
    for(y=TheRange[plot].ymin,j=0;j<ysize;j++,y+=1.0/(double)ISOSAMPLES){
      m[i][j] = function(plot,x,y);
    }
  }

  sprintf(buf,"binary%d",plot+1);
#ifdef UNIX
  if(!(fout = fopen(buf,"w")))
#else
  if(!(fout = fopen(buf,"wb")))
#endif
    int_error("Could not open file",0);
  else{
    fwrite_matrix(fout,m,0,xsize-1,0,ysize-1,rt,ct);
  }
  free_vector(rt,0,ysize-1);
  free_vector(ct,0,xsize-1);
  free_matrix(m,0,xsize-1,0,ysize-1);

  return(0);
}
