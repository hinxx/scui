
#include "sc.h"

bool sc_init()
{
    TRC("Enter\n");

    bool rv = sc_detect_thread_start();
    assert(rv != false);
    rv = sc_access_thread_start();
    assert(rv != false);

    TRC("Leave %d\n", rv);
    return rv;
}

void sc_destroy()
{
    TRC("Enter\n")
    
    sc_detect_thread_stop();
    sc_access_thread_stop();

    TRC("Leave\n");
}
