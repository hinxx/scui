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
static PSCARD_IO_REQUEST _card_protocol;

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

void scard_reset_reader_state()
{
    TRC("clearing reader state..\n");
    memset(_reader_name, 0, SC_MAX_READERNAME_LEN);
    memset(_reader_firmware, 0, SC_MAX_FIRMWARE_LEN);
    _reader_max_send = 0;
    _reader_max_recv = 0;
    _reader_card_types = 0;
    _reader_selected_card = 0;
    _reader_card_status = 0;
    _reader_state = 0;
}

void scard_reset_card_state()
{
    TRC("clearing card state..\n");
    _card_protocol = 0;
}

bool scard_connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle)
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

void scard_disconnect_card(PSCARDHANDLE handle)
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

bool scard_get_reader_info(const SCARDHANDLE handle)
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

bool scard_select_memory_card(const SCARDHANDLE handle)
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

bool scard_get_error_counter(const SCARDHANDLE handle, LPBYTE pin1, LPBYTE pin2, LPBYTE pin3, LPBYTE pin_retries)
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
    *pin_retries = recv_data[0];
    *pin1 = recv_data[1];
    *pin2 = recv_data[2];
    *pin3 = recv_data[3];
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("PIN retries left (should be 7!!): %u\n", *pin_retries);
    DBG("PIN bytes (valid after present): %02X %02X %02X\n", *pin1, *pin2, *pin3);
    return true;
}

bool scard_read_user_data(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len)
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
    // response is recv_len bytes long
    assert(recv_len == len);
    memcpy(data, recv_data, len);

    return true;
}

bool scard_present_pin(const SCARDHANDLE handle, BYTE pin1, BYTE pin2, BYTE pin3, LPBYTE pin_retries)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.7. PRESENT_CODE_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0x20, 0x00, 0x00, 0x03, pin1, pin2, pin3};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(handle, send_data, send_len, recv_data, &recv_len, sw_data);
    if (! rv) {
        return false;
    }
    *pin_retries = sw_data[1];
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("PIN retries left (should be 7!!): %u\n", *pin_retries);
    // success is 90 07
    rv = check_sw(sw_data, 0x90, 0x07);
    if (! rv) {
        return false;
    }
    // response is 0 bytes long
    // assert(recv_len == 0);
    return true;
}

bool scard_change_pin(const SCARDHANDLE handle, LPBYTE pin)
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
    // assert(recv_len == 0);
    DBG("PIN changed!\n");
    return true;
}

bool scard_write_card(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len)
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
    // assert(recv_len == 0);
    return true;
}
