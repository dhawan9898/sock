#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "rsvp_db.h"
#include "rsvp_sh.h"

extern int rsvpd_main();
int main(int argc, char *argv[]) {
    char *prog_name = strrchr(argv[0], '/');
    if (prog_name) prog_name++; else prog_name = argv[0];

    if (strcmp(prog_name, "rsvpd") == 0) {
        return rsvpd_main();
    } else if (strcmp(prog_name, "rsvpsh") == 0) {
        return rsvpsh_main();
    } else {
        fprintf(stderr, "Run as 'rsvpd' or 'rsvpsh' (e.g., via symlink)\n");
        return EXIT_FAILURE;
    }
    return 0;
}
