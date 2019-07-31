
#include "sc_util.h"

static SCARDCONTEXT l_context = 0;
static pthread_t l_thread_id = 0;
static bool l_run = true;

/**
 * Thread routine which executes card access requests. 
 * @param  ptr NULL
 * @return     0
 */
static void *sc_access_thread_fnc(void *ptr)
{
    TRC("Enter\n");

    LONG rv = sc_create_context(&l_context);
    DBG("create CONTEXT 0x%08lX\n", l_context);

    while (l_run) {
        TRC("loop, waiting for request ..\n");
        (void)fflush(stdout);

        TRC("sleepy ..\n");
        sleep(5);
        TRC("awake ..\n");

        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        if (rv == SCARD_E_CANCELLED) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    DBG("destroy CONTEXT 0x%08lX\n", l_context);
    sc_destroy_context(&l_context);

    TRC("Leave\n");
    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return true - success, false - failure
 */
bool sc_access_thread_start()
{
    TRC("Enter\n");

    int rv = pthread_create(&l_thread_id, NULL, sc_access_thread_fnc, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        TRC("Leave false\n");
        return false;
    }

    TRC("Leave true\n");
    return true;
}

/**
 * Stop the reader and card state change thread.
 * @return none
 */
void sc_access_thread_stop()
{
    TRC("Enter\n");

    // cancel waiting SCardEstablishContext() inside the thread
    if (l_context) {
        LONG rv = SCardCancel(l_context);
        CHECK("SCardCancel", rv);
    }
    l_run = false;
    pthread_join(l_thread_id, NULL);

    TRC("Leave\n");
}
