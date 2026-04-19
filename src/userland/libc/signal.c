#include "signal.h"
#include "errno.h"

static sighandler_t _signal_table[32] = {0};

__attribute__((weak)) sighandler_t signal(int sig, sighandler_t handler) {
    sighandler_t old;
    if (sig < 0 || sig >= (int)(sizeof(_signal_table) / sizeof(_signal_table[0]))) {
        errno = EINVAL;
        return SIG_ERR;
    }
    old = _signal_table[sig];
    _signal_table[sig] = handler;
    return old;
}
