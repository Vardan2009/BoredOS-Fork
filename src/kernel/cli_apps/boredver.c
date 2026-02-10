#include "cli_utils.h"

void cli_cmd_boredver(char *args) {
    (void)args;
    cli_write("BrewOS v1.50 Beta\n");
    cli_write("BrewOS Kernel V2.4.0 Beta\n");
}
