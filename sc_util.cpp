
#include "sc_util.h"

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


LONG sc_create_context(PSCARDCONTEXT context)
{
    TRC("Enter\n");
    // establish PC/SC Connection
    LONG rv = SCardEstablishContext(SCARD_SCOPE_SYSTEM, NULL, NULL, context);
    RETURN("SCardEstablishContext", rv);
    rv = SCardIsValidContext(*context);
    RETURN("SCardIsValidContext", rv);

    TRC("Leave 0x%08lX\n", rv);
    return rv;
}

LONG sc_destroy_context(PSCARDCONTEXT context)
{
    LONG rv = SCARD_S_SUCCESS;

    TRC("Enter\n");
    if (*context) {
        rv = SCardReleaseContext(*context);
        CHECK("SCardReleaseContext", rv);
        *context = 0;
    }

    TRC("Leave 0x%08lX\n", rv);
    return rv;
}

LONG sc_detect_reader(const SCARDCONTEXT context)
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
    return rv;
}

LONG sc_wait_for_reader(const SCARDCONTEXT context, const ULONG timeout)
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
    return rv;
}

LONG sc_wait_for_card(const SCARDCONTEXT context, const ULONG timeout)
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
    return rv;
}

LONG sc_probe_for_card(const SCARDCONTEXT context)
{
    return sc_wait_for_card(context, 1);
}

LONG sc_wait_for_card_remove(const SCARDCONTEXT context)
{
    return sc_wait_for_card(context, INFINITE);
}

LONG sc_wait_for_card_insert(const SCARDCONTEXT context)
{
    return sc_wait_for_card(context, INFINITE);
}

bool sc_is_reader_attached()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    bool rv = (g_sc_reader_name[0] != 0) ? true : false;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

bool sc_is_card_inserted()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    bool rv = (g_sc_reader_state & SCARD_STATE_PRESENT) ? true : false;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

LPSTR sc_get_reader_name()
{
    return g_sc_reader_name;
}

LONG sc_connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle)
{
    DWORD dwActiveProtocol;
    LONG rv = SCardConnect(context, g_sc_reader_name, SCARD_SHARE_SHARED,
        SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1, handle, &dwActiveProtocol);
    // CHECK("SCardConnect", rv);
    // if (rv != SCARD_S_SUCCESS) {
    //     qCritical() << "failed to connect to card!";
    //     return false;
    // }
    RETURN("SCardConnect", rv);

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
        return SCARD_E_PROTO_MISMATCH;
    }

    DBG("connected to card!\n");
    return rv;
}

void sc_disconnect_card(PSCARDHANDLE handle)
{
    LONG rv = SCardDisconnect(*handle, SCARD_UNPOWER_CARD);
    CHECK("SCardDisconnect", rv);
    // ignore return status
    g_sc_pci = 0;
    *handle = 0;
    DBG("disconnected from card!\n");
}

void sc_to_hex(LPBYTE data, ULONG len)
{
    int off = 0;
    memset(l_hexbuf, 0, 3*SC_BUFFER_MAXLEN);
    for (ULONG i = 0; i < len; i++) {
        off += sprintf(l_hexbuf + off, "%02X ", data[i]);
    }
}

LONG sc_do_xfer(const SCARDHANDLE handle, const LPBYTE send_data, const ULONG send_len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data)
{
    // dump request
    sc_to_hex(send_data, send_len);
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
    sc_to_hex(tmp_buf, tmp_len);
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
