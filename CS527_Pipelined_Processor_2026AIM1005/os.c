#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include "os.h"
#include "compiler.h"
#include "memory.h"
#ifdef _WIN32
#define strcasecmp _stricmp
#else
#include <strings.h>
#endif

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#include <strings.h>
#endif

typedef enum
{
    TASK_FREE,
    TASK_READY,
    TASK_RUNNING,
    TASK_FINISHED,
    TASK_WAITING
} TaskState;
typedef struct
{
    int pid;
    int proc_id;
    TaskState state;
    char program_source[256];
    char program_byte[256];
    char data_file[256];
    char original_data_file[256];
} Task;

static Task tasks[MAX_TASKS];
static int next_pid = 1;
static int shell_exit = 0;
static int shell_prompt = 1;

static int processor_is_free(int proc_id)
{
    int i;
    if (proc_id < 0 || proc_id >= NP)
        return 0;
    for (i = 0; i < MAX_TASKS; ++i)
        if (tasks[i].state != TASK_FREE && tasks[i].state != TASK_FINISHED &&
            tasks[i].proc_id == proc_id)
            return 0;
    return 1;
}

static int free_processor(void)
{
    int i;
    for (i = 0; i < NP; ++i)
        if (processor_is_free(i))
            return i;
    return -1;
}

/* Deterministic mapping for the first four PIDs: PID 1->P0, PID 2->P1,
 * PID 3->P2, PID 4->P3. Later PIDs reuse processors as they become free. */
static int preferred_processor(int pid)
{
    if (pid >= 1 && pid <= NP)
        return pid - 1;
    return -1;
}

void os_init(void)
{
    int i;
    memset(tasks, 0, sizeof(tasks));
    for (i = 0; i < MAX_TASKS; ++i)
    {
        tasks[i].state = TASK_FREE;
        tasks[i].proc_id = -1;
    }
    next_pid = 1;
    shell_exit = 0;
    shell_prompt = 1;
    memory_system_init();
}

static void make_task_filename(char *out, size_t n, const char *prefix, int pid, const char *ext) { snprintf(out, n, "%s_%d%s", prefix, pid, ext); }

void os_stop_accepting_tasks(void)
{
    shell_exit = 1;
}

int loader(const char *program_file, const char *data_file)
{
    int i, p = -1;
    char generated[256], copied_data[256];
    FILE *in, *out;
    int ch;
    for (i = 0; i < MAX_TASKS; ++i)
        if (tasks[i].state == TASK_FREE)
            break;
    if (i == MAX_TASKS)
    {
        fprintf(stderr, "OS: task table full.\n");
        return 0;
    }
    tasks[i].pid = next_pid++;

    p = preferred_processor(tasks[i].pid);
    if (p < 0 || !processor_is_free(p))
        p = free_processor();
    if (p < 0)
    {
        fprintf(stderr, "OS: all processors busy; task placed in waiting queue.\n");
        p = -1;
    }

    tasks[i].proc_id = p;
    tasks[i].state = (p >= 0) ? TASK_READY : TASK_WAITING;
    strncpy(tasks[i].program_source, program_file, 255);
    tasks[i].program_source[255] = '\0';
    strncpy(tasks[i].data_file, data_file, 255);
    tasks[i].data_file[255] = '\0';
    strncpy(tasks[i].original_data_file, data_file, 255);
    tasks[i].original_data_file[255] = '\0';
    make_task_filename(generated, sizeof(generated), "program", tasks[i].pid, ".byte");
    make_task_filename(copied_data, sizeof(copied_data), "data", tasks[i].pid, ".byte");
    strncpy(tasks[i].program_byte, generated, 255);
    tasks[i].program_byte[255] = '\0';

    if (strstr(program_file, ".txt") != NULL)
    {
        if (!compile_file(program_file, generated))
        {
            tasks[i].state = TASK_FREE;
            return 0;
        }
    }
    else
    {
        FILE *pin = fopen(program_file, "rb"), *pout = fopen(generated, "wb");
        if (!pin || !pout)
        {
            if (pin)
                fclose(pin);
            if (pout)
                fclose(pout);
            perror(program_file);
            tasks[i].state = TASK_FREE;
            return 0;
        }
        while ((ch = fgetc(pin)) != EOF)
            fputc(ch, pout);
        fclose(pin);
        fclose(pout);
    }
    in = fopen(data_file, "rb");
    if (!in)
    {
        perror(data_file);
        tasks[i].state = TASK_FREE;
        return 0;
    }
    out = fopen(copied_data, "wb");
    if (!out)
    {
        perror(copied_data);
        fclose(in);
        tasks[i].state = TASK_FREE;
        return 0;
    }
    while ((ch = fgetc(in)) != EOF)
        fputc(ch, out);
    fclose(in);
    fclose(out);
    /* Store the private working data path in the task only through data_file copy. */
    strncpy(tasks[i].data_file, copied_data, 255);
    tasks[i].data_file[255] = '\0';

    if (p >= 0)
    {
        int mem_result = memory_initialize(p, tasks[i].program_byte, tasks[i].data_file);

        if (mem_result == 1)
        {
            reset_processor(p);
            tasks[i].state = TASK_READY;
        }
        else if (mem_result == -1)
        {
            /* Processor is available, but physical frames are not. */
            tasks[i].proc_id = -1;
            tasks[i].state = TASK_WAITING;
            printf("OS: PID %d waiting for physical memory.\n", tasks[i].pid);
        }
        else
        {
            tasks[i].state = TASK_FREE;
            return 0;
        }
    }

    if (tasks[i].state == TASK_READY)
        printf("OS: loaded PID %d on processor %d\n", tasks[i].pid, p);
    else
        printf("OS: loaded PID %d into waiting queue\n", tasks[i].pid);
    return tasks[i].pid;
}

static void assign_waiting_tasks(void)
{
    int i, p;
    for (i = 0; i < MAX_TASKS; ++i)
    {
        if (tasks[i].state != TASK_WAITING)
            continue;
        p = preferred_processor(tasks[i].pid);
        if (p < 0 || !processor_is_free(p))
            p = free_processor();
        if (p < 0)
            return;
        {
            int mem_result = memory_initialize(p, tasks[i].program_byte, tasks[i].data_file);

            if (mem_result == -1)
            {
                /* Not enough frames yet. Keep the task waiting. */
                return;
            }
            if (mem_result == 0)
            {
                fprintf(stderr, "OS: failed to load waiting task PID %d.\\n", tasks[i].pid);
                tasks[i].state = TASK_FINISHED;
                continue;
            }

            reset_processor(p);
            tasks[i].proc_id = p;
            tasks[i].state = TASK_READY;
            printf("OS: PID %d assigned to processor %d from waiting queue.\n",
                   tasks[i].pid, p);
        }
    }
}

static void cleanup_finished(void)
{
    int i;
    for (i = 0; i < MAX_TASKS; ++i)
        if (tasks[i].state == TASK_FINISHED)
        {
            /* Files are intentionally retained so the submitted project is easy to debug. */
            tasks[i].state = TASK_FREE;
            tasks[i].proc_id = -1;
        }
}

#ifdef _WIN32
static HANDLE shell_stdin = NULL;

static int input_available(void)
{
    DWORD events = 0;
    if (!shell_stdin)
        shell_stdin = GetStdHandle(STD_INPUT_HANDLE);
    if (shell_stdin == INVALID_HANDLE_VALUE || shell_stdin == NULL)
        return 0;
    if (!GetNumberOfConsoleInputEvents(shell_stdin, &events))
        return 0;
    return events > 0;
}

static int read_shell_char(void)
{
    INPUT_RECORD rec;
    DWORD read = 0;
    if (!shell_stdin)
        shell_stdin = GetStdHandle(STD_INPUT_HANDLE);
    while (ReadConsoleInputA(shell_stdin, &rec, 1, &read))
    {
        if (rec.EventType != KEY_EVENT || !rec.Event.KeyEvent.bKeyDown)
            continue;
        {
            KEY_EVENT_RECORD *k = &rec.Event.KeyEvent;
            if (k->wVirtualKeyCode == VK_RETURN)
                return '\n';
            if (k->wVirtualKeyCode == VK_BACK)
                return '\b';
            if (k->uChar.AsciiChar)
                return (unsigned char)k->uChar.AsciiChar;
        }
    }
    return EOF;
}
#else
static int input_available(void)
{
    struct timeval tv = {0, 0};
    fd_set set;
    FD_ZERO(&set);
    FD_SET(STDIN_FILENO, &set);
    return select(STDIN_FILENO + 1, &set, NULL, NULL, &tv) > 0;
}

static int read_shell_char(void)
{
    return getchar();
}
#endif

static void shell_process_line(char *line)
{
    char *a = line, *b;
    char prog[256], data[256];
    size_t n;

    while (isspace((unsigned char)*a))
        ++a;
    n = strlen(a);
    while (n > 0 && isspace((unsigned char)a[n - 1]))
        a[--n] = '\0';
    if (!*a)
        return;

    b = a;
    while (*b && !isspace((unsigned char)*b))
        ++b;
    if (*b == '\0' && !strcasecmp(a, "exit"))
    {
        shell_exit = 1;
        printf("\nShell exiting; existing tasks will continue.\n");
        return;
    }

    if (sscanf(a, "%255s %255s", prog, data) == 2)
    {
        if (!loader(prog, data))
            printf("\nOS: failed to load task: %s %s\n", prog, data);
    }
    else
    {
        printf("\nUsage: <program.txt> <data.byte>\n");
    }
}

static void shell_poll(void)
{
    static char line[512];
    static int len = 0;
    while (input_available())
    {
        int ch = read_shell_char();
        if (ch == EOF)
        {
            if (len > 0)
            {
                line[len] = '\0';
                shell_process_line(line);
                len = 0;
            }
            if (shell_exit)
                return;
            break;
        }
        if (ch == '\r' || ch == '\n')
        {
            line[len] = '\0';
            shell_process_line(line);
            len = 0;
            if (!shell_exit)
            {
                printf("$ ");
                fflush(stdout);
            }
            else
                return;
        }
        else if (ch == '\b' || ch == 127)
        {
            if (len > 0)
            {
                --len;
                printf("\b \b");
                fflush(stdout);
            }
        }
        else if (isprint((unsigned char)ch) && len < (int)sizeof(line) - 1)
        {
            line[len++] = (char)ch;
            putchar(ch);
            fflush(stdout);
        }
    }
}

static void shell_show_prompt(void)
{
    if (shell_prompt && !shell_exit)
    {
        printf("$ ");
        fflush(stdout);
        shell_prompt = 0;
    }
}

void os_run(void)
{
    int i;
    shell_show_prompt();
    while (1)
    {
        int active = 0;
        if (!shell_exit)
            shell_poll();
        for (i = 0; i < MAX_TASKS; ++i)
        {
            if (tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING)
            {
                active = 1;
                tasks[i].state = TASK_RUNNING;
                process_instructions(tasks[i].proc_id, TIME_SLICE);
                if (end_of_simulation[tasks[i].proc_id])
                {
                    printf("\nOS: PID %d finished on processor %d\n", tasks[i].pid, tasks[i].proc_id);
                    memory_finalize(tasks[i].proc_id, tasks[i].data_file);
                    {
                        FILE *in = fopen(tasks[i].data_file, "rb"), *out = fopen(tasks[i].original_data_file, "wb");
                        int ch;
                        if (in && out)
                        {
                            while ((ch = fgetc(in)) != EOF)
                                fputc(ch, out);
                        }
                        if (in)
                            fclose(in);
                        if (out)
                            fclose(out);
                    }
                    close_processor_log(tasks[i].proc_id);
                    tasks[i].state = TASK_FINISHED;
                    tasks[i].proc_id = -1;
                }
                else
                    tasks[i].state = TASK_READY;
            }
        }
        assign_waiting_tasks();
        cleanup_finished();
        if (shell_exit && !active)
        {
            int waiting = 0;
            for (i = 0; i < MAX_TASKS; ++i)
                if (tasks[i].state == TASK_WAITING || tasks[i].state == TASK_READY || tasks[i].state == TASK_RUNNING)
                    waiting = 1;
            if (!waiting)
                break;
        }
#ifdef _WIN32
        Sleep(1);
#else
        {
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
        }
#endif
    }
}

void os_shutdown(void)
{
    int i;
    for (i = 0; i < NP; ++i)
        close_processor_log(i);
}
