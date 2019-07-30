
#include "sc_util.h"

static SCARDCONTEXT g_sc_thread_context = 0;
static pthread_t g_sc_thread = 0;

/**
 * Thread routine which waits for change in reader and card state. 
 * @param  ptr NULL
 * @return     ???
 */
static void *thread_function(void *ptr)
{
    TRC("Enter\n");

    LONG rv = sc_create_context(&g_sc_thread_context);

    while (1) {
        TRC("loop, waiting for reader to arrive or leave ..\n");
        (void)fflush(stdout);

        rv = sc_detect_reader(g_sc_thread_context);
        BOOL have_reader = sc_is_reader_attached();
        if (have_reader) {
            DBG("READER %s\n", sc_reader_name());
            DBG("probing for card..\n");
            sc_probe_for_card(g_sc_thread_context);
            BOOL have_card = sc_is_card_inserted();
            if (have_card) {
                DBG("CARD PRESENT..\n");
                DBG("waiting for card remove..\n");
                rv = sc_wait_for_card_remove(g_sc_thread_context);
            } else {
                DBG("NO CARD!\n");
                DBG("waiting for card insert..\n");
                rv = sc_wait_for_card_insert(g_sc_thread_context);
            }
        } else {
            DBG("NO READER\n");
            DBG("waiting for reader..\n");
            rv = sc_wait_for_reader(g_sc_thread_context, INFINITE);
        }

        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        if (rv == SCARD_E_CANCELLED) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    sc_destroy_context(&g_sc_thread_context);

    TRC("Leave 0\n");
    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return 0 - success, 1 - failure
 */
LONG sc_thread_start()
{
    TRC("Enter\n");

    int rv = pthread_create(&g_sc_thread, NULL, thread_function, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        TRC("Leave 1\n");
        return 1;
    }

    TRC("Leave 0\n");
    return 0;
}

/**
 * Stop the reader and card state change thread.
 * @return 0 - success, 1 - failure
 */
LONG sc_thread_stop()
{
    TRC("Enter\n");

    // cancel waiting SCardEstablishContext() inside the thread
    if (g_sc_thread_context) {
        LONG rv = SCardCancel(g_sc_thread_context);
        CHECK("SCardCancel", rv);
    }
    pthread_join(g_sc_thread, NULL);

    TRC("Leave 0\n");
    return 0;
}
