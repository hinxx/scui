
#include "sc_util.h"

static SCARDCONTEXT l_context = 0;
static pthread_t l_thread_id = 0;
static bool l_run = true;
static pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t l_condition = PTHREAD_COND_INITIALIZER;

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
    assert(rv == SCARD_S_SUCCESS);

    while (l_run) {
        TRC("loop, waiting for request ..\n");
        (void)fflush(stdout);

        TRC("waiting for condition ..\n");
        // Lock mutex and then wait for signal to relase mutex
        pthread_mutex_lock(&l_mutex);
        // mutex gets unlocked if condition variable is signaled
        pthread_cond_wait(&l_condition, &l_mutex);        
        pthread_mutex_unlock(&l_mutex);
        TRC("condition met!\n");

        // XXX not calling any SCardGetStatusChange() in this thread
        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        // if (rv == SCARD_E_CANCELLED) {
        //     TRC("stopping thread ..\n");
        //     break;
        // }
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

    // XXX not calling any SCardGetStatusChange() in this thread
    // // cancel waiting SCardEstablishContext() inside the thread
    // if (l_context) {
    //     LONG rv = SCardCancel(l_context);
    //     CHECK("SCardCancel", rv);
    // }

    // thread will exit after condition is set next
    l_run = false;

    pthread_mutex_lock(&l_mutex);
    // signal waiting thread by freeing the mutex
    pthread_cond_signal(&l_condition);
    pthread_mutex_unlock(&l_mutex);

    pthread_join(l_thread_id, NULL);

    TRC("Leave\n");
}
