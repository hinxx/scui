
#include "sc.h"

LONG sc_init()
{
    return sc_thread_start();
}

LONG sc_destroy()
{
    return sc_thread_stop();
}
