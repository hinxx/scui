
#include "sc_util.h"

static SCARDCONTEXT l_context = 0;
static pthread_t l_thread_id = 0;
static bool l_run = true;
static pthread_mutex_t l_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t l_condition = PTHREAD_COND_INITIALIZER;

#define SC_REQUEST_MAXLEN              128

#define SC_REQUEST_NONE                0
#define SC_REQUEST_CONNECT             1
#define SC_REQUEST_DISCONNECT          2

static ULONG l_req_id = 0;
static ULONG l_req_id_handled = 0;
static ULONG l_req = SC_REQUEST_NONE;
static ULONG l_req_len = 0;
static BYTE l_req_data[SC_REQUEST_MAXLEN] = {0};
static SCARDHANDLE l_handle = 0;

// decalarations
static ULONG sc_handle_request(const ULONG req_id, const ULONG req, const LPBYTE req_data, const ULONG req_len);

/**
 * Thread routine which executes card access requests. 
 * @param  ptr NULL
 * @return     0
 */
static void *sc_access_thread_fnc(void *ptr)
{
    TRC("Enter\n");

    ULONG req;
    ULONG req_id;
    ULONG req_id_handled;
    ULONG req_len;
    BYTE req_data[SC_REQUEST_MAXLEN];

    LONG rv = sc_create_context(&l_context);
    DBG("create CONTEXT 0x%08lX\n", l_context);
    assert(rv == SCARD_S_SUCCESS);

    while (1) {
        TRC("loop, waiting for request ..\n");
        (void)fflush(stdout);

        TRC("waiting for condition ..\n");
        // Lock mutex and then wait for signal to relase mutex
        pthread_mutex_lock(&l_mutex);
        // mutex gets unlocked if condition variable is signaled
        pthread_cond_wait(&l_condition, &l_mutex);
        // this thread can now handle new request
        
        // reset handled request ID
        l_req_id_handled = 0;
        // make local copies of the new request data
        req_id = l_req_id;
        req = l_req;
        req_len = l_req_len;
        memcpy(req_data, l_req_data, l_req_len);
        pthread_mutex_unlock(&l_mutex);
        TRC("condition met!\n");

        if (! l_run) {
            TRC("stopping thread ..\n");
            break;
        }

        // invoke request handling function from this thread!
        req_id_handled = sc_handle_request(req_id, req, req_data, req_len);
        pthread_mutex_lock(&l_mutex);
        l_req_id_handled = req_id_handled; 
        pthread_mutex_unlock(&l_mutex);

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

static void sc_card_request_dump()
{
    char buf[3*SC_REQUEST_MAXLEN] = {0};
    int off = 0;
    for (ULONG i = 0; i < l_req_len; i++) {
        off += sprintf(buf + off, "%02X ", l_req_data[i]);
    }
    DBG("REQ ID %lu, REQ %ld, REQ_LEN %lu, REQ_DATA %s\n", l_req_id, l_req, l_req_len, buf);
}

// card request client (i.e. UI) interface

static ULONG sc_request(const ULONG req, const LPBYTE req_data, const ULONG req_len)
{
    TRC("Enter\n");
    pthread_mutex_lock(&l_mutex);
    // save new card request
    l_req = req;
    l_req_len = req_len;
    memcpy(l_req_data, req_data, req_len);
    l_req_id++;
    sc_card_request_dump();
    // signal waiting thread to handle the command
    pthread_cond_signal(&l_condition);
    pthread_mutex_unlock(&l_mutex);

    TRC("Leave %lu\n", l_req_id);
    return l_req_id;
}

ULONG sc_request_connect()
{
    return sc_request(SC_REQUEST_CONNECT, NULL, 0);
}

ULONG sc_request_disconnect()
{
    return sc_request(SC_REQUEST_DISCONNECT, NULL, 0);
}


// card request handlers

bool sc_is_card_connected()
{
    return (l_handle != 0) ? true : false;
}

static void sc_handle_request_connect()
{
    // only connect to card if not already connected
    if (l_handle) {
        ERR("already connected to card\n");
        return;
    }

    LONG rv = sc_card_connect(l_context, &l_handle);
    if (rv == SCARD_S_SUCCESS) {
        assert(l_handle != 0);
    }
}

static void sc_handle_request_disconnect()
{
    assert(l_handle != 0);
    sc_card_disconnect(&l_handle);
    assert(l_handle == 0);
}

static ULONG sc_handle_request(const ULONG req_id, const ULONG req, const LPBYTE req_data, const ULONG req_len)
{
    switch(req) {
    case SC_REQUEST_CONNECT:
        sc_handle_request_connect();
    break;
    case SC_REQUEST_DISCONNECT:
        sc_handle_request_disconnect();
    break;
    default:
        return 0;
    break;
    }

    return req_id;
}

bool sc_is_request_handled()
{
    pthread_mutex_lock(&l_mutex);
    bool rv = (l_req_id == l_req_id_handled) ? true : false;
    pthread_mutex_unlock(&l_mutex);
    return rv;
}
