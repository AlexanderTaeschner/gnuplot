#ifndef GNUPLOT_DATABLOCK_H
# define GNUPLOT_DATABLOCK_H
#include "eval.h"	/* for union argument */

void datablock_command(void);
void functionblock_command(void);
struct data_array *get_datablock(char *name);
struct data_array *new_data_array(void);
char *parse_datablock_name(void);
void gpfree_datablock(struct value *datablock_value);
void gpfree_functionblock(struct value *functionblock_value);
void append_to_datablock(struct value *datablock_value, const char * line);
void append_multiline_to_datablock(struct value *datablock_value, const char * lines);
void save_set_to_datablock(char *datablock_name);
int datablock_size(struct value *datablock_value);
void f_eval(union argument *arg);

#ifdef USE_FUNCTIONBLOCKS
/* Set by "return" command and pushed onto the evaluation stack by f_eval */
extern struct value eval_return_value;

/* Used by f_eval to pass parameters to a function block */
extern struct value eval_parameters[9];

/* Non-zero to allow values to remain on the evaluation stack
 * across a call to f_eval()
 */
extern int evaluate_inside_functionblock;
#else	/* USE_FUNCTIONBLOCKS */
  #define  evaluate_inside_functionblock FALSE

#endif	/* USE_FUNCTIONBLOCKS */

#endif	/* GNUPLOT_DATABLOCK_H */
