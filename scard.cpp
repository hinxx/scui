/**
 * 
 */


#include "scard.h"


#ifdef WIN32
const char *pcsc_stringify_error(const LONG rv)
{
    static char out[20];
    sprintf_s(out, sizeof(out), "0x%08X", rv);
    return out;
}
#endif

static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;
static char _reader_name[SC_MAX_READERNAME_LEN+1];
static char _reader_firmware[SC_MAX_FIRMWARE_LEN+1];
static BYTE _reader_max_send;
static BYTE _reader_max_recv;
static USHORT _reader_card_types;
static BYTE _reader_selected_card;
static BYTE _reader_card_status;
static LONG _reader_state;

bool scard_create_context(PSCARDCONTEXT context)
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

void scard_destroy_context(PSCARDCONTEXT context)
{
    if (*context) {
        LONG rv = SCardReleaseContext(*context);
        CHECK("SCardReleaseContext", rv);
        *context = 0;
    }
}

void scard_detect_reader(const SCARDCONTEXT context)
{
    DWORD dwReaders = SC_MAX_READERNAME_LEN;
    BYTE mszReaders[SC_MAX_READERNAME_LEN] = {0};
    LPSTR mszGroups = nullptr;

    LONG rv = SCardListReaders(context, mszGroups, (LPSTR)&mszReaders, &dwReaders);
    CHECK("SCardListReaders", rv);
    // save reader name (NULL if not detected)
    pthread_mutex_lock(&_mutex);
    strncpy(_reader_name, (LPCSTR)mszReaders, SC_MAX_READERNAME_LEN);
    pthread_mutex_unlock(&_mutex);
}

void scard_wait_for_reader(const SCARDCONTEXT context, const ULONG timeout)
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

void scard_wait_for_card(const SCARDCONTEXT context, const ULONG timeout)
{
    pthread_mutex_lock(&_mutex);
    char reader_name[SC_MAX_READERNAME_LEN] = {0};
    strncpy(reader_name, _reader_name, SC_MAX_READERNAME_LEN);
    LONG reader_state = _reader_state;
    pthread_mutex_unlock(&_mutex);

    SCARD_READERSTATE rgReaderStates[1];
    rgReaderStates[0].szReader = reader_name;
    rgReaderStates[0].dwCurrentState = reader_state;
    rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

    DBG("enter SCardGetStatusChange: timeout=%ld reader_state=0x%08lX\n", timeout, reader_state);
    LONG rv = SCardGetStatusChange(context, timeout, rgReaderStates, 1);
    CHECK("SCardGetStatusChange", rv);
    
    if (rv == SCARD_S_SUCCESS) {
        pthread_mutex_lock(&_mutex);
        _reader_state = rgReaderStates[0].dwEventState;
        pthread_mutex_unlock(&_mutex);
    }
    DBG("leave SCardGetStatusChange: reader_state=0x%08lX\n", _reader_state);
}

bool scard_reader_presence()
{
    pthread_mutex_lock(&_mutex);
    bool rv = (_reader_name[0] != 0) ? true : false;
    pthread_mutex_unlock(&_mutex);
    return rv;
}

bool scard_card_presence()
{
    pthread_mutex_lock(&_mutex);
    bool rv = (_reader_state & SCARD_STATE_PRESENT) ? true : false;
    pthread_mutex_unlock(&_mutex);
    return rv;
}

char *scard_reader_name()
{
    // caller should lock the access to reader_name
    // and copy the contents to local buffer (variable)
    return _reader_name;
}


#if 0
static bool connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle)
{
    DWORD dwActiveProtocol;
    pthread_mutex_lock(&_mutex);
    char reader_name[SC_MAX_READERNAME_LEN] = {0};
    strncpy(reader_name, _reader_name, SC_MAX_READERNAME_LEN);
    pthread_mutex_unlock(&_mutex);
    LONG rv = SCardConnect(context, reader_name, SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, handle, &dwActiveProtocol);
    CHECK("SCardConnect", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }

    PSCARD_IO_REQUEST card_protocol = nullptr;
    switch(dwActiveProtocol) {
    case SCARD_PROTOCOL_T0:
        card_protocol = (SCARD_IO_REQUEST *)SCARD_PCI_T0;
        DBG("using T0 protocol\n");
        break;

    case SCARD_PROTOCOL_T1:
        card_protocol = (SCARD_IO_REQUEST *)SCARD_PCI_T1;
        DBG("using T1 protocol\n");
        break;
    default:
        ERR("failed to get proper protocol\n");
        return false;
    }

    pthread_mutex_lock(&_mutex);
    _card_protocol = card_protocol;
    pthread_mutex_unlock(&_mutex);

    DBG("connected to card!\n");
    return true;
}

static void disconnect_card(PSCARDHANDLE handle)
{
    LONG rv = SCardDisconnect(*handle, SCARD_UNPOWER_CARD);
    CHECK("SCardDisconnect", rv);
    // ignore return status
    pthread_mutex_lock(&_mutex);
    _card_protocol = 0;
    pthread_mutex_unlock(&_mutex);
    *handle = 0;
    DBG("disconnected from card!\n");
}

static char s_hexbuf[3*SC_MAX_REQUEST_LEN] = {0};
static void to_hex(LPBYTE data, ULONG len)
{
    int off = 0;
    memset(s_hexbuf, 0, 3*SC_MAX_REQUEST_LEN);
    for (ULONG i = 0; i < len; i++) {
        off += sprintf(s_hexbuf + off, "%02X ", data[i]);
    }
}

static bool do_xfer(const SCARDHANDLE handle, const LPBYTE send_data, const ULONG send_len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // dump request
    to_hex(send_data, send_len);
    DBG("SEND: [%lu]: %s\n", send_len, s_hexbuf);

    BYTE tmp_buf[SC_MAX_REQUEST_LEN+1];
    // SW1 and SW2 will be added at the end of the response
    ULONG tmp_len = *recv_len + 2;
    assert(_card_protocol != 0);
    ULONG rv = SCardTransmit(handle, _card_protocol, send_data, send_len, NULL, tmp_buf, &tmp_len);
    CHECK("SCardTransmit", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }
    // dump response
    to_hex(tmp_buf, tmp_len);
    DBG("RECV: [%lu]: %s\n", tmp_len, s_hexbuf);

    // SW1 and SW2 are at the end of response
    tmp_len -= 2;
    // update receive data and length
    memcpy(recv_data, tmp_buf, tmp_len);
    memcpy(sw_data, tmp_buf + tmp_len, 2);
    *recv_len = tmp_len;

    return true;
}

static bool check_sw(const LPBYTE sw_data, const BYTE sw1, const BYTE sw2)
{
    if ((sw_data[0] == sw1) && (sw_data[1] == sw2)) {
        DBG("xfer SW OK!\n");
        return true;
    }
    // XXX: anything to do here if SW is not success.. print error?
    //      which SW error codes are possible?
    ERR("xfer SW error: %02X %02X != %02X %02X\n", sw_data[0], sw_data[1], sw1, sw2);

    return false;
}

static bool get_reader_info(const SCARDHANDLE handle)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.4.1. GET_READER_INFORMATION
    BYTE send_data[] = {0xFF, 0x09, 0x00, 0x00, 0x10};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is 16 bytes long
    assert(recv_len == 16);
    // 10 bytes of firmware version
    pthread_mutex_lock(&_mutex);
    memcpy(_reader_firmware, recv_data, SC_MAX_FIRMWARE_LEN);
    _reader_max_send = recv_data[10];
    _reader_max_recv = recv_data[11];
    _reader_card_types = (recv_data[12] << 8) | recv_data[13];
    _reader_selected_card = recv_data[14];
    _reader_card_status = recv_data[15];
    pthread_mutex_unlock(&_mutex);
    DBG("firmware: %s\n", _reader_firmware);
    DBG("send max %d bytes\n", _reader_max_send);
    DBG("recv max %d bytes\n", _reader_max_recv);
    DBG("card types 0x%04X\n", _reader_card_types);
    DBG("selected card 0x%02X\n", _reader_selected_card);
    DBG("card status %d\n", _reader_card_status);
    return true;
}

static bool select_memory_card(const SCARDHANDLE handle)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.1. SELECT_CARD_TYPE
    // working with memory cards of type SLE 4432, SLE 4442, SLE 5532, SLE 5542
    BYTE send_data[] = {0xFF, 0xA4, 0x00, 0x00, 0x01, 0x06};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is 0 bytes long
    DBG("card selected!\n");
    return true;
}

static bool get_error_counter(const SCARDHANDLE handle)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.3. READ_PRESENTATION_ERROR_COUNTER_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0xB1, 0x00, 0x00, 0x04};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is 4 bytes long
    assert(recv_len == 4);
    pthread_mutex_lock(&_mutex);
    _card_pin_retries = recv_data[0];
    _card_pin_code[0] = recv_data[1];
    _card_pin_code[1] = recv_data[2];
    _card_pin_code[2] = recv_data[3];
    pthread_mutex_unlock(&_mutex);
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("PIN retries left (should be 7!!): %u\n", _card_pin_retries);
    DBG("PIN bytes (valid after present): %02X %02X %02X\n", _card_pin_code[0],
        _card_pin_code[1], _card_pin_code[2]);
    return true;
}

static bool read_user_data(const SCARDHANDLE handle, BYTE address, BYTE len)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.2.READ_MEMORY_CARD
    BYTE send_data[] = {0xFF, 0xB0, 0x00, address, len};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is req_data[1] bytes long
    assert(recv_len == len);
    _user_magic = (recv_data[3] << 24  | recv_data[2] << 16  | recv_data[1] << 8  | recv_data[0]);
    _user_id    = (recv_data[7] << 24  | recv_data[6] << 16  | recv_data[5] << 8  | recv_data[4]);
    _user_total = (recv_data[11] << 24 | recv_data[10] << 16 | recv_data[9] << 8  | recv_data[8]);
    _user_value = (recv_data[15] << 24 | recv_data[14] << 16 | recv_data[13] << 8 | recv_data[12]);
    DBG("MAGIC: %u\n", _user_magic);
    DBG("CARD ID: %u\n", _user_id);
    DBG("TOTAL: %u\n", _user_total);
    DBG("VALUE: %u\n", _user_value);
    return true;
}

static bool present_pin(const SCARDHANDLE handle, LPBYTE pin)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.7. PRESENT_CODE_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0x20, 0x00, 0x00, 0x03, pin[0], pin[1], pin[2]};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 07
    rv = check_sw(sw_data, 0x90, 0x07);
    if (! rv) {
        return false;
    }
    // response is 0 bytes long
    assert(recv_len == 0);
    pthread_mutex_lock(&_mutex);
    _card_pin_retries = sw_data[1];
    pthread_mutex_unlock(&_mutex);
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("PIN retries left (should be 7!!): %u\n", _card_pin_retries);
    return true;
}

static bool change_pin(const SCARDHANDLE handle, LPBYTE pin)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.8. CHANGE_CODE_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0xD2, 0x00, 0x01, 0x03, pin[0], pin[1], pin[2]};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is 0 bytes long
    assert(recv_len == 0);
    DBG("PIN changed!\n");
    return true;
}

static bool write_card(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.5. WRITE_MEMORY_CARD
    BYTE send_data[SC_MAX_REQUEST_LEN+1];
    send_data[0] = 0xFF;
    send_data[1] = 0xD0;
    send_data[2] = 0x00;
    send_data[3] = address;
    send_data[4] = len;
    assert(len + 5 <= SC_MAX_REQUEST_LEN);
    memcpy(&send_data[5], data, len);
    ULONG send_len = len + 5;
    // LONG rv = do_xfer(handle, send_data, send_len, recv_data, recv_len, sw_data);
    // return rv;
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    // success is 90 00
    rv = check_sw(sw_data, 0x90, 0x00);
    if (! rv) {
        return false;
    }
    // response is 0 bytes long
    assert(recv_len == 0);
    return true;
}



/**
 * reader and card detection thread
 */


static SCARDCONTEXT detect_context = 0;
static pthread_t detect_thread_id = 0;
static bool detect_thread_run = true;


static void reset_reader_state()
{
    memset(_reader_name, 0, SC_MAX_READERNAME_LEN);
    memset(_reader_firmware, 0, SC_MAX_FIRMWARE_LEN);
    _reader_max_send = 0;
    _reader_max_recv = 0;
    _reader_card_types = 0;
    _reader_selected_card = 0;
    _reader_card_status = 0;
    _reader_state = 0;
}

static void reset_card_state()
{
    _card_pin_retries = 0;
    _card_protocol = 0;
    _user_id = 0;
    _user_magic = 0;
    _user_value = 0;
    _user_total = 0;
}

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

    reset_reader_state();

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
                reset_card_state();
                wait_for_card_insert(detect_context);
            }
        } else {
            DBG("NO READER\n");
            DBG("waiting for reader..\n");
            reset_reader_state();
            reset_card_state();
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
static SCARDHANDLE worker_card = 0;

// #define SC_REQUEST_MAXLEN              128

#define SC_REQUEST_NONE                0
// #define SC_REQUEST_CONNECT             1
// #define SC_REQUEST_DISCONNECT          2
// #define SC_REQUEST_READER_INFO         3
// #define SC_REQUEST_SELECT_CARD         4
// #define SC_REQUEST_READ_CARD           5
// #define SC_REQUEST_ERROR_COUNTER       6
// #define SC_REQUEST_PRESENT_PIN         7
// #define SC_REQUEST_CHANGE_PIN          8
// #define SC_REQUEST_WRITE_CARD          9

#define SC_REQUEST_IDENTIFY_CARD          100
#define SC_REQUEST_UPDATE_CARD            200

// static ULONG l_req_id = 0;
// static ULONG l_req_id_handled = 0;
static uint32_t worker_request = SC_REQUEST_NONE;
// static ULONG l_req_len = 0;
// static BYTE l_req_data[SC_REQUEST_MAXLEN] = {0};

// decalarations
// static ULONG sc_handle_request(const ULONG req_id, const ULONG req, const LPBYTE req_data, const ULONG req_len);

/**
 * Thread routine which executes card access requests. 
 * @param  ptr NULL
 * @return     0
 */
static void *worker_thread_fnc(void *ptr)
{
    uint32_t request;
    // ULONG req_id;
    // ULONG req_id_handled;
    // ULONG req_len;
    // BYTE req_data[SC_REQUEST_MAXLEN];

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
        // l_req_id_handled = 0;
        // make local copies of the new request data
        // req_id = l_req_id;
        request = worker_request;
        // req_len = l_req_len;
        // memcpy(req_data, l_req_data, l_req_len);
        pthread_mutex_unlock(&worker_mutex);
        TRC("condition met!\n");

        if (! worker_thread_run) {
            TRC("stopping thread ..\n");
            break;
        }

        // invoke request handling function from this thread!
        // req_id_handled = sc_handle_request(req_id, req, req_data, req_len);
        // pthread_mutex_lock(&worker_mutex);
        // l_req_id_handled = req_id_handled; 
        // pthread_mutex_unlock(&worker_mutex);

        if (request == SC_REQUEST_IDENTIFY_CARD) {
            process_identify();
        } else if (request == SC_REQUEST_UPDATE_CARD) {
            process_update();
        }

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







static void do_request(const uint32_t request)
{
    TRC("Enter\n");
    pthread_mutex_lock(&worker_mutex);
    // save new card request
    worker_request = request;
    // l_req_len = req_len;
    // memcpy(l_req_data, req_data, req_len);
    // l_req_id++;
    // sc_card_request_dump();
    // signal waiting thread to handle the command
    pthread_cond_signal(&worker_condition);
    pthread_mutex_unlock(&worker_mutex);

    // TRC("Leave %lu\n", l_req_id);
    // return l_req_id;
}

static void request_identify()
{
    do_request(SC_REQUEST_IDENTIFY_CARD);
}

static void request_update()
{
    do_request(SC_REQUEST_UPDATE_CARD);
}

static bool process_identify()
{
    bool rv = false;

    rv = connect_card(worker_context, &worker_card);
    if (! rv) {
        return false;
    }

    rv = get_reader_info(worker_card);
    if (! rv) {
        return false;
    }

    rv = select_memory_card(worker_card);
    if (! rv) {
        return false;
    }

    rv = get_error_counter(worker_card);
    if (! rv) {
        return false;
    }

    // read from user data start
    BYTE address = 64;
    // read 16 bytes (4x 32-bit integers)
    BYTE length = 16;
    rv = read_user_data(worker_card, address, length);
    if (! rv) {
        return false;
    }

    return rv;
}

static bool process_update()
{
    bool rv = false;

    rv = get_error_counter(worker_card);
    if (! rv) {
        return false;
    }

    // blank card has all bytes set to 0xFF, check user magic
    if (_user_magic == 0xFFFFFFFF) {
        DBG("user magic is not set yet, need to present default PIN..\n");
        BYTE pin[3] = {0};
        // use default PIN here!!!
        pin[0] = 0xFF;
        pin[1] = 0xFF;
        pin[2] = 0xFF;
        rv = present_pin(worker_card, pin);
        if (! rv) {
            return false;
        }

        rv = get_error_counter(worker_card);
        if (! rv) {
            return false;
        }

        // use our PIN here!!!
        pin[0] = SC_PIN_CODE_BYTE_1;
        pin[1] = SC_PIN_CODE_BYTE_2;
        pin[2] = SC_PIN_CODE_BYTE_3;
        rv = change_pin(worker_card, pin);
        if (! rv) {
            return false;
        }

        // TODO
        // write ID, magic, value & total 0

    } else {
        DBG("user magic is set, need to present our PIN..\n");
        // before PIN is presented, returned PIN code from error check
        // is 0x00 0x00 0x00, afterwards is our PIN code; present code 
        // only once.
        if (! ((_card_pin_code[0] == SC_PIN_CODE_BYTE_1)
            && (_card_pin_code[1] == SC_PIN_CODE_BYTE_2)
            && (_card_pin_code[2] == SC_PIN_CODE_BYTE_3))) {
            BYTE pin[3] = {0};
            // use our PIN here!!!
            pin[0] = SC_PIN_CODE_BYTE_1;
            pin[1] = SC_PIN_CODE_BYTE_2;
            pin[2] = SC_PIN_CODE_BYTE_3;
            rv = present_pin(worker_card, pin);
            if (! rv) {
                return false;
            }

            rv = get_error_counter(worker_card);
            if (! rv) {
                return false;
            }
        }

        uint32_t value = 0;
        // what type of card is requested?
        if (_user_admin_card) {
            // set admin ID
            _user_id = SC_ADMIN_ID;
        } else {
            // set regular ID
            _user_id = SC_REGULAR_ID;
            // add new value to remaining user value
            value = _user_value + _user_add_value;
        }
        // set value and total to be equal
        _user_value = value;
        _user_total = value;
        // always use latest magic value!
        _user_magic = SC_MAGIC_VALUE;

        // read from user data start
        BYTE address = 64;
        // read 16 bytes (4x 32-bit integers)
        BYTE length = 16;
        // data, 4x 32-bit integer in order: magic, ID, total, value
        BYTE data[16] = {0};
        data[0] =  (BYTE)(_user_magic         & 0xFF);
        data[1] =  (BYTE)((_user_magic >> 8)  & 0xFF);
        data[2] =  (BYTE)((_user_magic >> 16) & 0xFF);
        data[3] =  (BYTE)((_user_magic >> 24) & 0xFF);
        data[4] =  (BYTE)(_user_id            & 0xFF);
        data[5] =  (BYTE)((_user_id    >> 8)  & 0xFF);
        data[6] =  (BYTE)((_user_id    >> 16) & 0xFF);
        data[7] =  (BYTE)((_user_id    >> 24) & 0xFF);
        data[8] =  (BYTE)(_user_total         & 0xFF);
        data[9] =  (BYTE)((_user_total >> 8)  & 0xFF);
        data[10] = (BYTE)((_user_total >> 16) & 0xFF);
        data[11] = (BYTE)((_user_total >> 24) & 0xFF);
        data[12] = (BYTE)(_user_value         & 0xFF);
        data[13] = (BYTE)((_user_value >> 8)  & 0xFF);
        data[14] = (BYTE)((_user_value >> 16) & 0xFF);
        data[15] = (BYTE)((_user_value >> 24) & 0xFF);
        rv = write_card(worker_card, address, data, length);
        if (! rv) {
            return false;
        }
        DBG("Card updated, new value/total %u!\n", _user_value);
    }

    return true;
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

bool sc_is_card_connected()
{
    return (worker_card != 0) ? true : false;
}

char *sc_get_reader_name()
{
    return reader_name();
}

void sc_identify_card()
{
    TRC("Enter\n")
    request_identify();
}

void sc_forget_card()
{
    TRC("Enter\n")
    disconnect_card(&worker_card);
}

void sc_get_user_data(uint32_t *magic, uint32_t *id, uint32_t *value, uint32_t *total)
{
    // TRC("Enter\n")
    *magic = _user_magic;
    *id = _user_id;
    *value = _user_value;
    *total = _user_total;
}

void sc_set_user_data(bool want_admin, uint32_t new_value)
{
    TRC("Enter\n")
    // do we want regular or admin card?
    _user_admin_card = want_admin;
    // store newly bought credit
    _user_add_value = new_value;
    DBG("creating %s card\n", _user_admin_card ? "ADMIN" : "REGULAR");
    DBG("adding %d E to the card (if REGULAR)\n", _user_add_value);
    request_update();
}
#endif
