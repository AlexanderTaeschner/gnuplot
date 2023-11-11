void multiplot_start(void);
void multiplot_end(void);
void multiplot_next(void);
void multiplot_reset(void);
void append_multiplot_line(char *line);
void replay_multiplot(void);
void multiplot_reset_after_error(void);
int multiplot_current_panel(void);

extern TBOOLEAN multiplot_playback;	/* TRUE while inside "remultiplot" playback */
