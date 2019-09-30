

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// PC/SC lite
#ifdef __APPLE__
#include <PCSC/winscard.h>
#include <PCSC/wintypes.h>
#else
#include <winscard.h>
#endif

#include "helpers.h"

// check status and print error if any
#define CHECK(f, rv) \
    if (SCARD_S_SUCCESS != rv) \
    { \
        static char out[100]; \
        sprintf(out, "%s() failed with: '%s'", f, pcsc_stringify_error(rv)); \
        ERR("%s\n", out); \
    }

// check status, print error if any and then return
#define RETURN(f, rv) \
    if (SCARD_S_SUCCESS != rv) \
    { \
        static char out[100]; \
        sprintf(out, "%s() failed with: '%s'", f, pcsc_stringify_error(rv)); \
        ERR("%s\n", out); \
        TRC("Leave %ld\n", rv); \
        return rv; \
    }

static SCARDCONTEXT _context = 0;
static SCARDHANDLE _card = 0;
// static pthread_mutex_t _mutex = PTHREAD_MUTEX_INITIALIZER;

#define SC_MAX_REQUEST_LEN              255
#define SC_MAX_READERNAME_LEN           128
#define SC_MAX_FIRMWARE_LEN             10

static char _reader_name[SC_MAX_READERNAME_LEN+1];
static LONG _reader_state = 0;
static PSCARD_IO_REQUEST _card_protocol = 0;
static char _reader_firmware[SC_MAX_FIRMWARE_LEN+1];
static BYTE _reader_max_send;
static BYTE _reader_max_recv;
static USHORT _reader_card_types;
static BYTE _reader_selected_card;
static BYTE _reader_card_status;
static BYTE _card_pin_retries;
static BYTE _card_pin_code[3];
static uint32_t _user_magic;
static uint32_t _user_id;
static uint32_t _user_total;
static uint32_t _user_value;

bool scard_create_context()
{
    // establish PC/SC Connection
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, &_context);
    CHECK("SCardEstablishContext", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }
    rv = SCardIsValidContext(_context);
    CHECK("SCardIsValidContext", rv);
    if (rv != SCARD_S_SUCCESS) {
        return false;
    }

    DBG("created CONTEXT 0x%08lX\n", _context);
    return true;
}

void scard_destroy_context()
{
    if (_context) {
        DBG("destroying CONTEXT 0x%08lX\n", _context);
        LONG rv = SCardReleaseContext(_context);
        CHECK("SCardReleaseContext", rv);
        _context = 0;
    }
}

void scard_detect_reader()
{
    DWORD dwReaders = SC_MAX_READERNAME_LEN;
    BYTE mszReaders[SC_MAX_READERNAME_LEN] = {0};
    LPSTR mszGroups = nullptr;

    LONG rv = SCardListReaders(_context, mszGroups, (LPSTR)&mszReaders, &dwReaders);
    CHECK("SCardListReaders", rv);
    // save reader name (NULL if not detected)
    // pthread_mutex_lock(&g_state_mutex);
    strncpy(_reader_name, (LPCSTR)mszReaders, SC_MAX_READERNAME_LEN);
    // pthread_mutex_unlock(&g_state_mutex);
}

bool scard_reader_presence()
{
    // pthread_mutex_lock(&g_state_mutex);
    bool rv = (_reader_name[0] != 0) ? true : false;
    // pthread_mutex_unlock(&g_state_mutex);
    return rv;
}

char *scard_reader_name()
{
    // caller should lock the access to reader_name
    // and copy the contents to local buffer (variable)
    return _reader_name;
}

static void wait_for_card(const ULONG timeout)
{
    // pthread_mutex_lock(&g_state_mutex);
    char reader_name[SC_MAX_READERNAME_LEN] = {0};
    strncpy(reader_name, _reader_name, SC_MAX_READERNAME_LEN);
    LONG reader_state = _reader_state;
    // pthread_mutex_unlock(&g_state_mutex);

    SCARD_READERSTATE rgReaderStates[1];
    rgReaderStates[0].szReader = reader_name;
    rgReaderStates[0].dwCurrentState = reader_state;
    rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

    DBG("enter SCardGetStatusChange: timeout=%ld reader_state=0x%08lX\n", timeout, reader_state);
    LONG rv = SCardGetStatusChange(_context, timeout, rgReaderStates, 1);
    CHECK("SCardGetStatusChange", rv);
    
    if (rv == SCARD_S_SUCCESS) {
        // pthread_mutex_lock(&g_state_mutex);
        _reader_state = rgReaderStates[0].dwEventState;
        // pthread_mutex_unlock(&g_state_mutex);
    }
    DBG("leave SCardGetStatusChange: reader_state=0x%08lX\n", _reader_state);
}

void scard_probe_for_card()
{
    wait_for_card(1);
}

bool scard_card_presence()
{
    // pthread_mutex_lock(&g_state_mutex);
    bool rv = (_reader_state & SCARD_STATE_PRESENT) ? true : false;
    // pthread_mutex_unlock(&g_state_mutex);
    return rv;
}

static bool connect_card()
{
    DWORD dwActiveProtocol;
    // pthread_mutex_lock(&g_state_mutex);
    char reader_name[SC_MAX_READERNAME_LEN] = {0};
    strncpy(reader_name, _reader_name, SC_MAX_READERNAME_LEN);
    // pthread_mutex_unlock(&g_state_mutex);
    LONG rv = SCardConnect(_context, reader_name, SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, &_card, &dwActiveProtocol);
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

    // pthread_mutex_lock(&g_state_mutex);
    _card_protocol = card_protocol;
    // pthread_mutex_unlock(&g_state_mutex);

    DBG("connected to card!\n");
    return true;
}

static void disconnect_card()
{
    LONG rv = SCardDisconnect(_card, SCARD_UNPOWER_CARD);
    CHECK("SCardDisconnect", rv);
    // ignore return status
    // pthread_mutex_lock(&g_state_mutex);
    _card_protocol = 0;
    // pthread_mutex_unlock(&g_state_mutex);
    _card = 0;
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


static bool do_xfer(const LPBYTE send_data, const ULONG send_len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // dump request
    to_hex(send_data, send_len);
    DBG("SEND: [%lu]: %s\n", send_len, s_hexbuf);

    BYTE tmp_buf[SC_MAX_REQUEST_LEN+1];
    // SW1 and SW2 will be added at the end of the response
    ULONG tmp_len = *recv_len + 2;
    assert(_card_protocol != 0);
    ULONG rv = SCardTransmit(_card, _card_protocol, send_data, send_len, NULL, tmp_buf, &tmp_len);
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

static bool get_reader_info()
{
    // REF-ACR38x-CCID-6.05.pdf, 9.4.1. GET_READER_INFORMATION
    BYTE send_data[] = {0xFF, 0x09, 0x00, 0x00, 0x10};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(send_data, send_len, recv_data, &recv_len, sw_data);
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
    // pthread_mutex_lock(&g_state_mutex);
    memcpy(_reader_firmware, recv_data, SC_MAX_FIRMWARE_LEN);
    _reader_max_send = recv_data[10];
    _reader_max_recv = recv_data[11];
    _reader_card_types = (recv_data[12] << 8) | recv_data[13];
    _reader_selected_card = recv_data[14];
    _reader_card_status = recv_data[15];
    // pthread_mutex_unlock(&g_state_mutex);
    DBG("firmware: %s\n", _reader_firmware);
    DBG("send max %d bytes\n", _reader_max_send);
    DBG("recv max %d bytes\n", _reader_max_recv);
    DBG("card types 0x%04X\n", _reader_card_types);
    DBG("selected card 0x%02X\n", _reader_selected_card);
    DBG("card status %d\n", _reader_card_status);
    return true;
}

static bool select_memory_card()
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.1. SELECT_CARD_TYPE
    // working with memory cards of type SLE 4432, SLE 4442, SLE 5532, SLE 5542
    BYTE send_data[] = {0xFF, 0xA4, 0x00, 0x00, 0x01, 0x06};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(send_data, send_len, recv_data, &recv_len, sw_data);
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

static bool get_error_counter()
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.3. READ_PRESENTATION_ERROR_COUNTER_MEMORY_CARD
    // for SLE 4442 and SLE 5542 memory cards
    BYTE send_data[] = {0xFF, 0xB1, 0x00, 0x00, 0x04};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(send_data, send_len, recv_data, &recv_len, sw_data);
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
    // pthread_mutex_lock(&g_state_mutex);
    _card_pin_retries = recv_data[0];
    _card_pin_code[0] = recv_data[1];
    _card_pin_code[1] = recv_data[2];
    _card_pin_code[2] = recv_data[3];
    // pthread_mutex_unlock(&g_state_mutex);
    // counter value: 0x07          indicates success,
    //                0x03 and 0x01 indicate failed verification
    //                0x00          indicates locked card (no retries left)
    DBG("PIN retries left (should be 7!!): %u\n", _card_pin_retries);
    DBG("PIN bytes (valid after present): %02X %02X %02X\n", _card_pin_code[0],
        _card_pin_code[1], _card_pin_code[2]);
    return true;
}

static bool read_user_data(BYTE address, BYTE len)
{
    // REF-ACR38x-CCID-6.05.pdf, 9.3.6.2.READ_MEMORY_CARD
    BYTE send_data[] = {0xFF, 0xB0, 0x00, address, len};
    ULONG send_len = sizeof(send_data);
    BYTE recv_data[SC_MAX_REQUEST_LEN+1] = {0};
    ULONG recv_len = sizeof(recv_data);
    BYTE sw_data[2+1] = {0};
    bool rv = do_xfer(send_data, send_len, recv_data, &recv_len, sw_data);
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

bool scard_card_identify()
{
    bool rv = false;

    if (_card) {
        disconnect_card();
    }

    rv = connect_card();
    if (! rv) {
        return false;
    }

    rv = get_reader_info();
    if (! rv) {
        disconnect_card();
        return false;
    }

    rv = select_memory_card();
    if (! rv) {
        disconnect_card();
        return false;
    }

    rv = get_error_counter();
    if (! rv) {
        disconnect_card();
        return false;
    }

    // read from user data start
    BYTE address = 64;
    // read 16 bytes (4x 32-bit integers)
    BYTE length = 16;
    rv = read_user_data(address, length);
    if (! rv) {
        disconnect_card();
        return false;
    }

    return rv;
}
