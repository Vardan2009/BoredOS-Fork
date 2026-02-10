#include "cli_utils.h"

// Forward declaration from cmd.c
extern void pager_wrap_content(const char **lines, int count);
extern void pager_set_mode(void);

const char* manual_pages[] = {
    "No manual round here pal",
 
};
const int manual_num_lines = sizeof(manual_pages) / sizeof(char*);

void cli_cmd_man(char *args) {
    (void)args;
    pager_wrap_content(manual_pages, manual_num_lines);
    pager_set_mode();
}
