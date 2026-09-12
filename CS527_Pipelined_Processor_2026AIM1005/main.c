#include <stdio.h>
#include "compiler.h"
#include "os.h"

int main(int argc, char **argv)
{
    os_init();

    if (argc >= 3)
    {
        printf("Starting CS527 Lab 5 simulator.\n");

        if (!loader(argv[1], argv[2]))
            return 1;

        /*
         * Command-line mode accepts one initial task and then lets the OS
         * finish it normally.
         */
        os_stop_accepting_tasks();
    }
    else
    {
        printf("CS527 Lab 5 multi-processing simulator\n");
        printf("Shell input: program.txt data.byte\n");
        printf("Type exit to stop accepting new tasks.\n");
    }

    os_run();
    os_shutdown();

    printf("Simulation completed successfully.\n");
    return 0;
}
