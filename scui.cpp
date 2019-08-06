/**
 * 
 */


#include "sc_internal.h"


#ifdef WIN32
const char *pcsc_stringify_error(const LONG rv)
{
    static char out[20];
    sprintf_s(out, sizeof(out), "0x%08X", rv);
    return out;
}
#endif

static char g_sc_reader_name[MAX_READERNAME] = {0};
static LONG g_sc_reader_state = SCARD_STATE_UNAWARE;
static pthread_mutex_t g_sc_reader_mutex = PTHREAD_MUTEX_INITIALIZER;
// static BOOL g_sc_card_connected = 0;
// static SCARDHANDLE g_sc_card_handle = 0;
static PSCARD_IO_REQUEST g_sc_pci= 0;
static char l_hexbuf[3*SC_BUFFER_MAXLEN] = {0};


struct state state = {0};


/**
 * utilities
 */

static void to_hex(LPBYTE data, ULONG len)
{
    int off = 0;
    memset(l_hexbuf, 0, 3*SC_BUFFER_MAXLEN);
    for (ULONG i = 0; i < len; i++) {
        off += sprintf(l_hexbuf + off, "%02X ", data[i]);
    }
}



/**
 * low level smart card access
 */

static bool create_context(PSCARDCONTEXT context)
{
    // establish PC/SC Connection
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, context);
    CHECK("SCardEstablishContext", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }
    rv = SCardIsValidContext(*context);
    CHECK("SCardIsValidContext", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }

    return true;
}

static void destroy_context(PSCARDCONTEXT context)
{
    if (*context) {
        LONG rv = SCardReleaseContext(*context);
        CHECK("SCardReleaseContext", rv);
        *context = 0;
    }
}

static void detect_reader(const SCARDCONTEXT context)
{
    DWORD dwReaders = MAX_READERNAME;
    BYTE mszReaders[MAX_READERNAME] = {0};
    LPSTR mszGroups = nullptr;

    LONG rv = SCardListReaders(context, mszGroups, (LPSTR)&mszReaders, &dwReaders);
    CHECK("SCardListReaders", rv);
    // save reader name (NULL if not detected)
    pthread_mutex_lock(&g_sc_reader_mutex);
    strncpy(g_sc_reader_name, (LPCSTR)mszReaders, MAX_READERNAME);
    pthread_mutex_unlock(&g_sc_reader_mutex);
}

static void wait_for_reader(const SCARDCONTEXT context, const ULONG timeout)
{
    SCARD_READERSTATE rgReaderStates[1];

    rgReaderStates[0].szReader = "\\\\?PnP?\\Notification";
    rgReaderStates[0].dwCurrentState = SCARD_STATE_UNAWARE;
    rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

    DBG("enter SCardGetStatusChange: timeout=%ld dwEventState=0x%08lX dwCurrentState=0x%08lX\n",
        timeout, rgReaderStates[0].dwEventState, rgReaderStates[0].dwCurrentState);

    LONG rv = SCardGetStatusChange(context, timeout, rgReaderStates, 1);
    CHECK("SCardGetStatusChange", rv);
    DBG("leave SCardGetStatusChange: rv=0x%08lX dwEventState=0x%08lX dwCurrentState=0x%08lX\n",
        rv, rgReaderStates[0].dwEventState, rgReaderStates[0].dwCurrentState);
}

static void wait_for_card(const SCARDCONTEXT context, const ULONG timeout)
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    char reader_name[MAX_READERNAME] = {0};
    strncpy(reader_name, g_sc_reader_name, MAX_READERNAME);
    LONG reader_state = g_sc_reader_state;
    pthread_mutex_unlock(&g_sc_reader_mutex);

    SCARD_READERSTATE rgReaderStates[1];
    rgReaderStates[0].szReader = reader_name;
    rgReaderStates[0].dwCurrentState = reader_state;
    rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

    DBG("enter SCardGetStatusChange: timeout=%ld reader_state=0x%08lX\n", timeout, reader_state);
    LONG rv = SCardGetStatusChange(context, timeout, rgReaderStates, 1);
    CHECK("SCardGetStatusChange", rv);
    
    if (rv == SCARD_S_SUCCESS) {
        pthread_mutex_lock(&g_sc_reader_mutex);
        g_sc_reader_state = rgReaderStates[0].dwEventState;
        pthread_mutex_unlock(&g_sc_reader_mutex);
    }
    DBG("leave SCardGetStatusChange: reader_state=0x%08lX\n", g_sc_reader_state);
}

static void probe_for_card(const SCARDCONTEXT context)
{
    wait_for_card(context, 1);
}

static void wait_for_card_remove(const SCARDCONTEXT context)
{
    wait_for_card(context, INFINITE);
}

static void wait_for_card_insert(const SCARDCONTEXT context)
{
    wait_for_card(context, INFINITE);
}

static bool reader_presence()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    bool rv = (g_sc_reader_name[0] != 0) ? true : false;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

static bool card_presence()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    bool rv = (g_sc_reader_state & SCARD_STATE_PRESENT) ? true : false;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

static char *reader_name()
{
    return g_sc_reader_name;
}

static bool connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle)
{
    DWORD dwActiveProtocol;
    pthread_mutex_lock(&g_sc_reader_mutex);
    char reader_name[MAX_READERNAME] = {0};
    strncpy(reader_name, g_sc_reader_name, MAX_READERNAME);
    pthread_mutex_unlock(&g_sc_reader_mutex);
    LONG rv = SCardConnect(context, reader_name, SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, handle, &dwActiveProtocol);
    CHECK("SCardConnect", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }

    switch(dwActiveProtocol) {
    case SCARD_PROTOCOL_T0:
        g_sc_pci = (SCARD_IO_REQUEST *)SCARD_PCI_T0;
        DBG("using T0 protocol\n");
        break;

    case SCARD_PROTOCOL_T1:
        g_sc_pci = (SCARD_IO_REQUEST *)SCARD_PCI_T1;
        DBG("using T1 protocol\n");
        break;
    default:
        ERR("failed to get proper protocol\n");
        return false;
    }

    DBG("connected to card!\n");
    return true;
}

void disconnect_card(PSCARDHANDLE handle)
{
    LONG rv = SCardDisconnect(*handle, SCARD_UNPOWER_CARD);
    CHECK("SCardDisconnect", rv);
    // ignore return status
    g_sc_pci = 0;
    *handle = 0;
    DBG("disconnected from card!\n");
}

LONG sc_do_xfer(const SCARDHANDLE handle, const LPBYTE send_data, const ULONG send_len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // dump request
    to_hex(send_data, send_len);
    DBG("SEND: [%lu]: %s\n", send_len, l_hexbuf);

    BYTE tmp_buf[SC_BUFFER_MAXLEN];
    // SW1 and SW2 will be added at the end of the response
    ULONG tmp_len = *recv_len + 2;
    assert(g_sc_pci != 0);
    ULONG rv = SCardTransmit(handle, g_sc_pci, send_data, send_len, NULL, tmp_buf, &tmp_len);
    CHECK("SCardTransmit", rv);
    if (rv != SCARD_S_SUCCESS) {
        return rv;
    }
    // dump response
    to_hex(tmp_buf, tmp_len);
    DBG("RECV: [%lu]: %s\n", tmp_len, l_hexbuf);

    // SW1 and SW2 are at the end of response
    tmp_len -= 2;
    // update receive data and length
    memcpy(recv_data, tmp_buf, tmp_len);
    memcpy(sw_data, tmp_buf + tmp_len, 2);
    *recv_len = tmp_len;

    return rv;
}

LONG sc_check_sw(const LPBYTE sw_data, const BYTE sw1, const BYTE sw2)
{
    if ((sw_data[0] == sw1) && (sw_data[1] == sw2)) {
        DBG("card xfer OK!\n");
        return SCARD_S_SUCCESS;
    }
    // XXX: anything to do here if SW is not success.. print error?
    //      which SW error codes are possible?
    ERR("card xfer failed: %02X %02X\n", sw_data[0], sw_data[1]);

    return SCARD_F_UNKNOWN_ERROR;
}

LONG sc_get_reader_info(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.4.1. GET_READER_INFORMATION
    BYTE send_data[] = {0xFF, 0x09, 0x00, 0x00, 0x10};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_select_memory_card(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.1. SELECT_CARD_TYPE
    // working with memory cards of type SLE 4432, SLE 4442, SLE 5532, SLE 5542
    BYTE send_data[] = {0xFF, 0xA4, 0x00, 0x00, 0x01, 0x06};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_read_card(const SCARDHANDLE handle, BYTE address, BYTE len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.2.READ_MEMORY_CARD
    BYTE send_data[] = {0xFF, 0xB0, 0x00, address, len};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_get_error_counter(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.3. READ_PRESENTATION_ERROR_COUNTER_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0xB1, 0x00, 0x00, 0x04};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_present_pin(const SCARDHANDLE handle, LPBYTE pin, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.7. PRESENT_CODE_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0x20, 0x00, 0x00, 0x03, pin[0], pin[1], pin[2]};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_change_pin(const SCARDHANDLE handle, LPBYTE pin, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.8. CHANGE_CODE_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0xD2, 0x00, 0x01, 0x03, pin[0], pin[1], pin[2]};
    ULONG send_len = sizeof(send_data);
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}

LONG sc_write_card(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.5. WRITE_MEMORY_CARD
    BYTE send_data[SC_BUFFER_MAXLEN];
    send_data[0] = 0xFF;
    send_data[1] = 0xD0;
    send_data[2] = 0x00;
    send_data[3] = address;
    send_data[4] = len;
    assert(len + 5 <= SC_BUFFER_MAXLEN);
    memcpy(&send_data[5], data, len);
    ULONG send_len = len + 5;
    LONG rv = sc_do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    return rv;
}



/**
 * reader and card detection thread
 */


static SCARDCONTEXT detect_context = 0;
static pthread_t detect_thread_id = 0;
static bool detect_thread_run = true;

/**
 * Thread routine which waits for change in reader and card state. 
 * @param  ptr NULL
 * @return     0
 */
static void *detect_thread_fnc(void *ptr)
{
    bool rv = create_context(&detect_context);
    DBG("created CONTEXT 0x%08lX\n", detect_context);
    assert(rv != false);

    while (1) {
        TRC("loop, waiting for reader to arrive or leave ..\n");
        (void)fflush(stdout);

        detect_reader(detect_context);
        BOOL have_reader = reader_presence();
        if (have_reader) {
            DBG("READER %s\n", reader_name());
            DBG("probing for card..\n");
            probe_for_card(detect_context);
            BOOL have_card = card_presence();
            if (have_card) {
                DBG("CARD PRESENT..\n");
                DBG("waiting for card remove..\n");
                wait_for_card_remove(detect_context);
            } else {
                DBG("NO CARD!\n");
                DBG("waiting for card insert..\n");
                wait_for_card_insert(detect_context);
            }
        } else {
            DBG("NO READER\n");
            DBG("waiting for reader..\n");
            wait_for_reader(detect_context, INFINITE);
        }

        // exit the thread!
        if (! detect_thread_run) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    DBG("destroying CONTEXT 0x%08lX\n", detect_context);
    destroy_context(&detect_context);

    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return true - success, false - failure
 */
static bool detect_thread_start()
{
    int rv = pthread_create(&detect_thread_id, NULL, detect_thread_fnc, NULL);
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
static void detect_thread_stop()
{
    // thread will exit
    detect_thread_run = false;
    
    // cancel waiting SCardEstablishContext() inside the thread
    if (detect_context) {
        LONG rv = SCardCancel(detect_context);
        CHECK("SCardCancel", rv);
    }
    pthread_join(detect_thread_id, NULL);
    DBG("Destroyed detect thread\n");
}



/**
 * worker thread
 */



static SCARDCONTEXT worker_context = 0;
static pthread_t worker_thread_id = 0;
static bool worker_thread_run = true;
static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_condition = PTHREAD_COND_INITIALIZER;

#define SC_REQUEST_MAXLEN              128

#define SC_REQUEST_NONE                0
#define SC_REQUEST_CONNECT             1
#define SC_REQUEST_DISCONNECT          2
#define SC_REQUEST_READER_INFO         3
#define SC_REQUEST_SELECT_CARD         4
#define SC_REQUEST_READ_CARD           5
#define SC_REQUEST_ERROR_COUNTER       6
#define SC_REQUEST_PRESENT_PIN         7
#define SC_REQUEST_CHANGE_PIN          8
#define SC_REQUEST_WRITE_CARD          9

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
static void *worker_thread_fnc(void *ptr)
{
    ULONG req;
    ULONG req_id;
    ULONG req_id_handled;
    ULONG req_len;
    BYTE req_data[SC_REQUEST_MAXLEN];

    bool rv = create_context(&worker_context);
    DBG("created CONTEXT 0x%08lX\n", worker_context);
    assert(rv != false);

    while (1) {
        TRC("loop, waiting for request ..\n");
        (void)fflush(stdout);

        TRC("waiting for condition ..\n");
        // Lock mutex and then wait for signal to relase mutex
        pthread_mutex_lock(&worker_mutex);
        // mutex gets unlocked if condition variable is signaled
        pthread_cond_wait(&worker_condition, &worker_mutex);
        // this thread can now handle new request
        
        // reset handled request ID
        l_req_id_handled = 0;
        // make local copies of the new request data
        req_id = l_req_id;
        req = l_req;
        req_len = l_req_len;
        memcpy(req_data, l_req_data, l_req_len);
        pthread_mutex_unlock(&worker_mutex);
        TRC("condition met!\n");

        if (! worker_thread_run) {
            TRC("stopping thread ..\n");
            break;
        }

        // invoke request handling function from this thread!
        req_id_handled = sc_handle_request(req_id, req, req_data, req_len);
        pthread_mutex_lock(&worker_mutex);
        l_req_id_handled = req_id_handled; 
        pthread_mutex_unlock(&worker_mutex);

        // XXX not calling any SCardGetStatusChange() in this thread
        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        // if (rv == SCARD_E_CANCELLED) {
        //     TRC("stopping thread ..\n");
        //     break;
        // }
    }

    DBG("destroying CONTEXT 0x%08lX\n", worker_context);
    destroy_context(&worker_context);

    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return true - success, false - failure
 */
static bool worker_thread_start()
{
    int rv = pthread_create(&worker_thread_id, NULL, worker_thread_fnc, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        return false;
    }

    DBG("Created worker thread\n");
    return true;
}

/**
 * Stop the reader and card state change thread.
 * @return none
 */
static void worker_thread_stop()
{
    // thread will exit
    worker_thread_run = false;

    pthread_mutex_lock(&worker_mutex);
    // signal waiting thread by freeing the mutex
    pthread_cond_signal(&worker_condition);
    pthread_mutex_unlock(&worker_mutex);

    pthread_join(worker_thread_id, NULL);
    DBG("Destroyed worker thread\n");
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
    pthread_mutex_lock(&worker_mutex);
    // save new card request
    l_req = req;
    l_req_len = req_len;
    memcpy(l_req_data, req_data, req_len);
    l_req_id++;
    sc_card_request_dump();
    // signal waiting thread to handle the command
    pthread_cond_signal(&worker_condition);
    pthread_mutex_unlock(&worker_mutex);

    TRC("Leave %lu\n", l_req_id);
    return l_req_id;
}

bool sc_request_connect()
{
    return sc_request(SC_REQUEST_CONNECT, NULL, 0);
}

bool sc_request_disconnect()
{
    return sc_request(SC_REQUEST_DISCONNECT, NULL, 0);
}

bool sc_request_reader_info()
{
    return sc_request(SC_REQUEST_READER_INFO, NULL, 0);
}

bool sc_request_select_card()
{
    return sc_request(SC_REQUEST_SELECT_CARD, NULL, 0);
}

bool sc_request_read_card()
{
    BYTE data[2] = {0};
    // read from user data start
    data[0] = 64;
    // read 16 bytes (4x 32-bit integers)
    data[1] = 16;
    return sc_request(SC_REQUEST_READ_CARD, data, 2);
}

bool sc_request_error_counter()
{
    return sc_request(SC_REQUEST_ERROR_COUNTER, NULL, 0);
}

// FIXME: need to handle default pin (0xFF 0xFF 0xFF) presentation for blank cards!
bool sc_request_present_pin()
{
    BYTE data[3] = {0};
    // FIXME: do not hardcode PIN here!!!
    // 3 pin bytes
    data[0] = 0xC0;
    data[1] = 0xDE;
    data[2] = 0xA5;
    return sc_request(SC_REQUEST_PRESENT_PIN, data, 3);
}

bool sc_request_change_pin()
{
    BYTE data[3] = {0};
    // FIXME: do not hardcode PIN here!!!
    // 3 pin bytes
    data[0] = 0xC0;
    data[1] = 0xDE;
    data[2] = 0xA5;
    return sc_request(SC_REQUEST_CHANGE_PIN, data, 3);
}

bool sc_request_write_card()
{
    // FIXME: Where does data come from?
    //        Just a stub for now..
    return sc_request(SC_REQUEST_WRITE_CARD, NULL, 0);
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

    LONG rv = connect_card(worker_context, &l_handle);
    if (rv == SCARD_S_SUCCESS) {
        assert(l_handle != 0);
    }
}

static void sc_handle_request_disconnect()
{
    assert(l_handle != 0);
    disconnect_card(&l_handle);
    assert(l_handle == 0);
}

static void sc_handle_request_reader_info()
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    LONG rv = sc_get_reader_info(l_handle, recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is 16 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.4.1. GET_READER_INFORMATION
    assert(recv_len == 16);
    BYTE firmware[11] = {0};
    memcpy(firmware, recv_data, 10);
    DBG("firmware: %s\n", firmware);
    BYTE send_max = recv_data[10];
    DBG("send max %d bytes\n", send_max);
    BYTE recv_max = recv_data[11];
    DBG("recv max %d bytes\n", recv_max);
    USHORT card_types = (recv_data[12] << 8) | recv_data[13];
    DBG("card types 0x%04X\n", card_types);
    BYTE card_sel = recv_data[14];
    DBG("selected card 0x%02X\n", card_sel);
    BYTE card_stat = recv_data[15];
    DBG("card status %d\n", card_stat);
    rv = sc_check_sw(sw_data, 0x90, 0x00);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

static void sc_handle_request_select_card()
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    LONG rv = sc_select_memory_card(l_handle, recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is 0 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.1. SELECT_CARD_TYPE
    assert(recv_len == 0);
    rv = sc_check_sw(sw_data, 0x90, 0x00);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

static void sc_handle_request_read_card(const LPBYTE req_data, const ULONG req_len)
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    assert(req_len == 2);
    LONG rv = sc_read_card(l_handle, req_data[0], req_data[1], recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is req_data[1] bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.2.READ_MEMORY_CARD
    assert(recv_len == req_data[1]);
    // to_hex(recv_data, recv_len);
    ULONG magic = (recv_data[3] << 24  | recv_data[2] << 16  | recv_data[1] << 8  | recv_data[0]);
    ULONG id    = (recv_data[7] << 24  | recv_data[6] << 16  | recv_data[5] << 8  | recv_data[4]);
    ULONG total = (recv_data[11] << 24 | recv_data[10] << 16 | recv_data[9] << 8  | recv_data[8]);
    ULONG value = (recv_data[15] << 24 | recv_data[14] << 16 | recv_data[13] << 8 | recv_data[12]);
    DBG("MAGIC: %lu\n", magic);
    DBG("CARD ID: %lu\n", id);
    DBG("TOTAL: %lu\n", total);
    DBG("VALUE: %lu\n", value);
    rv = sc_check_sw(sw_data, 0x90, 0x00);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

static void sc_handle_request_error_counter()
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    LONG rv = sc_get_error_counter(l_handle, recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is 4 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.3. READ_PRESENTATION_ERROR_COUNTER_MEMORY_CARD
    assert(recv_len == 4);
    // to_hex(recv_data, recv_len);
    BYTE counter = recv_data[0];
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("ERROR COUNTER: %u\n", counter);
    rv = sc_check_sw(sw_data, 0x90, 0x00);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

static void sc_handle_request_present_pin(const LPBYTE req_data, const ULONG req_len)
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    assert(req_len == 3);
    LONG rv = sc_present_pin(l_handle, req_data, recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is 0 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.7. PRESENT_CODE_MEMORY_CARD
    assert(recv_len == 0);
    // to_hex(sw_data, 2);
    BYTE counter = sw_data[1];
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("ERROR COUNTER: %u\n", counter);
    rv = sc_check_sw(sw_data, 0x90, 0x07);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

static void sc_handle_request_change_pin(const LPBYTE req_data, const ULONG req_len)
{
    BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    BYTE sw_data[2] = {0};
    assert(l_handle != 0);
    assert(req_len == 3);
    LONG rv = sc_change_pin(l_handle, req_data, recv_data, &recv_len, sw_data);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
    // response is 0 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.8. CHANGE_CODE_MEMORY_CARD
    assert(recv_len == 0);
    rv = sc_check_sw(sw_data, 0x90, 0x00);
    if (rv != SCARD_S_SUCCESS) {
        return;
    }
}

// FIXME: untested!
//        Just a stub for now..
static void sc_handle_request_write_card(const LPBYTE req_data, const ULONG req_len)
{
    ERR("Not implemented!\n");
    // BYTE recv_data[SC_BUFFER_MAXLEN] = {0};
    // ULONG recv_len = SC_BUFFER_MAXLEN - 2;
    // BYTE sw_data[2] = {0};
    // assert(l_handle != 0);
    // LONG rv = sc_write_card(l_handle, req_data, recv_data, &recv_len, sw_data);
    // if (rv != SCARD_S_SUCCESS) {
    //     return;
    // }
    // // response is 0 bytes long, see REF-ACR38x-CCID-6.05.pdf, 9.3.6.5. WRITE_MEMORY_CARD
    // assert(recv_len == 0);
    // rv = sc_check_sw(sw_data, 0x90, 0x00);
    // if (rv != SCARD_S_SUCCESS) {
    //     return;
    // }
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
    case SC_REQUEST_READER_INFO:
        sc_handle_request_reader_info();
    break;
    case SC_REQUEST_SELECT_CARD:
        sc_handle_request_select_card();
    break;
    case SC_REQUEST_READ_CARD:
        sc_handle_request_read_card(req_data, req_len);
    break;
    case SC_REQUEST_ERROR_COUNTER:
        sc_handle_request_error_counter();
    break;
    case SC_REQUEST_PRESENT_PIN:
        sc_handle_request_present_pin(req_data, req_len);
    break;
    case SC_REQUEST_CHANGE_PIN:
        sc_handle_request_change_pin(req_data, req_len);
    break;
    case SC_REQUEST_WRITE_CARD:
        sc_handle_request_write_card(req_data, req_len);
    break;
    default:
        return 0;
    break;
    }

    return req_id;
}

bool sc_is_request_handled()
{
    pthread_mutex_lock(&worker_mutex);
    bool rv = (l_req_id == l_req_id_handled) ? true : false;
    pthread_mutex_unlock(&worker_mutex);
    return rv;
}



/**
 * exported user API
 */

bool sc_init()
{
    TRC("Enter\n");

    bool rv = detect_thread_start();
    assert(rv != false);
    rv = worker_thread_start();
    assert(rv != false);

    TRC("Leave %d\n", rv);
    return rv;
}

void sc_destroy()
{
    TRC("Enter\n")
    
    detect_thread_stop();
    worker_thread_stop();

    TRC("Leave\n");
}

bool sc_is_reader_attached()
{
    return reader_presence();
}

bool sc_is_card_inserted()
{
    return card_presence();
}

char *sc_get_reader_name()
{
    return reader_name();
}
