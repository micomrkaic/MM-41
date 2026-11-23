#ifndef MM41_WEB_HISTORY_H
#define MM41_WEB_HISTORY_H

void add_history(const char *line);
int  read_history(const char *file);
int  write_history(const char *file);
void stifle_history(int max);
void clear_history(void);

#endif
