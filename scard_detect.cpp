/**
 * 
 */


#include "scard.h"

static SCARDCONTEXT _context = 0;
static pthread_t _thread_id = 0;
static bool _thread_run = true;

static void reset_reader_state()
{
    // memset(g_state.reader_name, 0, SC_MAX_READERNAME_LEN);
    // memset(g_state.reader_firmware, 0, SC_MAX_FIRMWARE_LEN);
    // g_state.reader_max_send = 0;
    // g_state.reader_max_recv = 0;
    // g_state.reader_card_types = 0;
    // g_state.reader_selected_card = 0;
    // g_state.reader_card_status = 0;
    // g_state.reader_state = 0;
}

static void reset_card_state()
{
    // g_state.card_pin_retries = 0;
    // g_state.card_protocol = 0;
    // g_state.user_id = 0;
    // g_state.user_magic = 0;
    // g_state.user_value = 0;
    // g_state.user_total = 0;
}

static void *thread_fnc(void *ptr)
{
    bool rv = scard_create_context(&_context);
    DBG("created CONTEXT 0x%08lX\n", _context);
    assert(rv != false);

    reset_reader_state();

    while (1) {
        TRC("loop, waiting for reader to arrive or leave ..\n");
        (void)fflush(stdout);

        scard_detect_reader(_context);
        BOOL have_reader = scard_reader_presence();
        if (have_reader) {
            DBG("READER %s\n", scard_reader_name());
            DBG("probing for card..\n");
            // probe_for_card(detect_context);
            scard_wait_for_card(_context, 1);
            BOOL have_card = scard_card_presence();
            if (have_card) {
                DBG("CARD PRESENT..\n");
                DBG("waiting for card remove..\n");
                // wait_for_card_remove(detect_context);
                scard_wait_for_card(_context, INFINITE);
            } else {
                DBG("NO CARD!\n");
                DBG("waiting for card insert..\n");
                reset_card_state();
                // wait_for_card_insert(detect_context);
                scard_wait_for_card(_context, INFINITE);
            }
        } else {
            DBG("NO READER\n");
            DBG("waiting for reader..\n");
            reset_reader_state();
            reset_card_state();
            scard_wait_for_reader(_context, INFINITE);
        }

        // exit the thread!
        if (! _thread_run) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    DBG("destroying CONTEXT 0x%08lX\n", _context);
    scard_destroy_context(&_context);

    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return true - success, false - failure
 */
bool scard_detect_thread_start()
{
    int rv = pthread_create(&_thread_id, NULL, thread_fnc, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        return false;
    }

    DBG("Created detect thread\n");
    return true;
}

/**
 * Stop the reader and card state change thread.
 * @return none
 */
void scard_detect_thread_stop()
{
    // thread will exit
    _thread_run = false;
    
    // cancel waiting SCardEstablishContext() inside the thread
    if (_context) {
        LONG rv = SCardCancel(_context);
        CHECK("SCardCancel", rv);
    }
    pthread_join(_thread_id, NULL);
    DBG("Destroyed detect thread\n");
}