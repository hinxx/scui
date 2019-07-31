
#include "sc_util.h"

static SCARDCONTEXT l_context = 0;
static pthread_t l_thread_id = 0;

/**
 * Thread routine which waits for change in reader and card state. 
 * @param  ptr NULL
 * @return     0
 */
static void *sc_detect_thread_fnc(void *ptr)
{
    TRC("Enter\n");

    LONG rv = sc_create_context(&l_context);
    DBG("create CONTEXT 0x%08lX\n", l_context);
    assert(rv == SCARD_S_SUCCESS);

    while (1) {
        TRC("loop, waiting for reader to arrive or leave ..\n");
        (void)fflush(stdout);

        rv = sc_detect_reader(l_context);
        BOOL have_reader = sc_is_reader_attached();
        if (have_reader) {
            DBG("READER %s\n", sc_get_reader_name());
            DBG("probing for card..\n");
            sc_probe_for_card(l_context);
            BOOL have_card = sc_is_card_inserted();
            if (have_card) {
                DBG("CARD PRESENT..\n");
                DBG("waiting for card remove..\n");
                rv = sc_wait_for_card_remove(l_context);
            } else {
                DBG("NO CARD!\n");
                DBG("waiting for card insert..\n");
                rv = sc_wait_for_card_insert(l_context);
            }
        } else {
            DBG("NO READER\n");
            DBG("waiting for reader..\n");
            rv = sc_wait_for_reader(l_context, INFINITE);
        }

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
bool sc_detect_thread_start()
{
    TRC("Enter\n");

    int rv = pthread_create(&l_thread_id, NULL, sc_detect_thread_fnc, NULL);
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
void sc_detect_thread_stop()
{
    TRC("Enter\n");

    // cancel waiting SCardEstablishContext() inside the thread
    if (l_context) {
        LONG rv = SCardCancel(l_context);
        CHECK("SCardCancel", rv);
    }
    pthread_join(l_thread_id, NULL);

    TRC("Leave\n");
}
