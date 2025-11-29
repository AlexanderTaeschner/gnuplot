/*
  The files demo_plugin.c and gnuplot_plugin.h serve as templates
  showing how to create an external shared object containing functions
  that can be accessed from inside a gnuplot session using the import
  command.  For example, after compiling this file into a shared
  object (gnuplot_plugin.so or gnuplot_plugin.dll) the functions it
  contains can be imported as shown:
 
  gnuplot> import divisors(x) from "demo_plugin"
  gnuplot> import mysinc(x) from "demo_plugin:sinc"
  gnuplot> import nsinc(N,x) from "demo_plugin"
 */

#include "gnuplot_plugin.h"
#include <math.h>

/* This function returns the number of divisors of the first argument */

DLLEXPORT struct value divisors(int nargs, struct value *arg, void *p)
{
  int a = IVAL(arg[0]);
  int i = 1;
  int j = a;
  struct value r;
  r.type = INTGR;
  r.v.int_val = 0;

  /* Enforce a match between the number of parameters declared
   * by the gnuplot import command and the number implemented here.
   */
  RETURN_ERROR_IF_WRONG_NARGS(r, nargs, 1);

  /* Sanity check on argument type */
  RETURN_ERROR_IF_NONNUMERIC(r, arg[0]);

  while (i <= j)
    {
      if (a == i*j)
	{
	  r.v.int_val += 1 + (i!=j);
	}
      i++;
      j=a/i;
    }

  return r;
}

DLLEXPORT struct value sinc(int nargs, struct value *arg, void *p)
{
  double x = RVAL(arg[0]);
  struct value r;
  r.type = CMPLX;

  /* Enforce a match between the number of parameters declared
   * by the gnuplot import command and the number implemented here.
   */
  RETURN_ERROR_IF_WRONG_NARGS(r, nargs, 1);

  /* Sanity check on argument type */
  RETURN_ERROR_IF_NONNUMERIC(r, arg[0]);

  r.v.cmplx_val.real = sin(x)/x;
  r.v.cmplx_val.imag = 0.0;
  
  return r;
}

DLLEXPORT struct value nsinc(int nargs, struct value *arg, void *p)
{
  struct value r;
  int n;	/* 1st parameter */
  double x;	/* 2nd parameter */

  /* Enforce a match between the number of parameters declared
   * by the gnuplot import command and the number implemented here.
   */
  RETURN_ERROR_IF_WRONG_NARGS(r, nargs, 2);

  /* Sanity check on argument type */
  RETURN_ERROR_IF_NONNUMERIC(r, arg[0]);
  RETURN_ERROR_IF_NONNUMERIC(r, arg[1]);

  n = IVAL(arg[0]);
  x = RVAL(arg[1]);

  r.type = CMPLX;
  r.v.cmplx_val.real = n * sin(x)/x;
  r.v.cmplx_val.imag = 0.0;
  
  return r;
}

/*
 * Now show the use of an initializer function and a corresponding destructor function.
 * These are always named gnuplot_init() and gnuplot_fini().
 * Both are optional.
 *
 * The initializer is called by gnuplot when a plugin function from this
 * shared object is imported:
 *       gnuplot> import newdata() from "demo_plugin"
 * The initializer can allocate and initialize any data structure, files,
 * pipes, etc, that will be needed when the plugin function is later called
 * from gnuplot.  These would typically be wrapped into a single dynamically
 * allocated structure whose address is passed back to gnuplot as an opaque
 * token.
 *
 * The destructor is called by gnuplot when the plugin function goes out of use.
 * This happens if
 * - the function is redefined via another "import"
 * - the function is redefined from the command line
 * - the program exits normally (FIXME: not yet implemented)
 */

#include <stdio.h>	/* for the debugging fprintf statements */
#include <stdlib.h>	/* for malloc() */
#include <string.h>	/* for memcpy() */
#include <time.h>	/* to generate the timestamp */

/* Prototype for exported function */
struct value newdata(int n, struct value *v, void *p);

DLLEXPORT void *gnuplot_init(exfn_t exported_function)
{
    struct value *private;

    /* Template for a 2-element gnuplot array */
    struct value gparray[3] = {
	{.type = NOTDEFINED, .v.array_header = {.size = 2, .parent = NULL}},
	{.type = STRING, .v.string_val = NULL},
	{.type = CMPLX, .v.cmplx_val = {0.0, 0.0}}
    };

    /* Several functions are exported by this file but this initialization
     * is needed for only one of them.
     */
    if (exported_function != newdata)
	return NULL;

    /* Allocate space for a 2-element gnuplot ARRAY structure.
     * Each call to newdata() will write into this array and return it
     * to gnuplot.
     */
    private = malloc(sizeof(struct value));
    private->type = ARRAY;
    private->v.value_array = malloc(3 * sizeof(struct value));
    memcpy( private->v.value_array, gparray, sizeof(gparray) );

    return (void *) private;
}

/*
 *
 */
DLLEXPORT void gnuplot_fini(exfn_t exported_function, void *p)
{
    struct value *private = p;
    struct value *data;

    /* Several functions are exported by this file but this destructor 
     * is needed for only one of them.
     */
    if (exported_function != newdata)
	return;

    data = private->v.value_array;
    free( data[1].v.string_val );
    free( private->v.value_array );
    free( private );
}

/*
 * Get some data that would be difficult for gnuplot to access directly.
 * For the purpose of this example that is a numerical value and a timestamp.
 * Calling newdata(N) from gnuplot will return a 2-element array.
 * N is expected to be an integer that indicates, perhaps, which sensor
 * to read or which of several options to execute in obtaining the data value.
 * The first element returned array is a STRING holding the timestamp.
 * The second element of the returned array is a CMPLX value.
 */

DLLEXPORT struct value newdata(int n, struct value *v, void *p)
{
    struct value *private = (struct value *)p;
    struct value *data;
    int N;
    char buffer[128];
    double sensor_reading;

    /* We are expecting that newdata(N) was called with a single
     * parameter N that was an integer.  If that is not the case
     * we will just pretend that N=0 was passed in.
     * A more complicated function could tailor the return
     * based on whatever was passed in.
     */
    if ((n < 1) || (v->type != INTGR)) {
	N = 0;
    } else {
	N = v->v.int_val;
	if (N < 0 || N > 99)
	    N = 0;
    }

    /* Free any content in the array left over from a previous call. */
    data = private->v.value_array;
    if (data[1].type == STRING)
	free(data[1].v.string_val);
    else {
	/* Our private structure has become corrupted! */
    }

    /* construct a timestamp string to return as the first array element */
    if (1) {
	struct tm tm;
	struct timespec now;
	time_t sec;
	double seconds;
	double millisec;

	clock_gettime( CLOCK_REALTIME, &now );
	sec = now.tv_sec;
	gmtime_r( &sec, &tm );
	millisec = now.tv_nsec / 1000000U;
	seconds = tm.tm_sec + millisec / 1000.;
        snprintf(buffer, sizeof(buffer), "%02d:%02d:%06.3f",
		(int)(tm.tm_hour), (int)(tm.tm_min), seconds);
    } else {
	struct tm tm;
	time_t now;
	time(&now);
	strftime(buffer, sizeof(buffer), "%H:%M:%S %F", gmtime_r(&now, &tm));
    }
    data[1].type = STRING;
    data[1].v.string_val = strdup(buffer);

    /* Query something to get a value.
     * It's hard to come up with anything interesting that isn't system-dependent
     * and thus unsuitable for a generic demonostrtation.
     * Here we simulate a sensor that always returns roughly "42" :-)
     * A query of the current CPU package temperature that works on my machine
     * is offered [if WORKS_FOR_ME is defined] as a more interesting example.
     */ 
    sensor_reading = 42. + (double)rand() / (double)RAND_MAX;

//  #define WORKS_FOR_ME 1
    #ifdef WORKS_FOR_ME
	if (1) {
	    FILE *fp = popen("sensors -j k10temp-pci-00c3 | grep temp1", "r");
	    char *temp;
	    fgets(buffer, sizeof(buffer), fp);
	    temp = strchr(buffer, ':');
	    sensor_reading = atof(temp+1);
	    pclose(fp);
	} else
    #endif

    data[2].type = CMPLX;
    data[2].v.cmplx_val.real = sensor_reading;
    data[2].v.cmplx_val.imag = 0.0;

    return *private;
}

