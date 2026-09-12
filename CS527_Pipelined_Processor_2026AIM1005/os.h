#ifndef OS_H
#define OS_H

#include "processor.h"

#define MAX_TASKS 64
#define TIME_SLICE 10

void os_init(void);
void os_run(void);
void os_shutdown(void);
int loader(const char *program_file, const char *data_file);
void os_stop_accepting_tasks(void);

#endif
