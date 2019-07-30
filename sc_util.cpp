
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

BOOL sc_is_reader_attached()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    BOOL rv = (g_sc_reader_name[0] != 0) ? 1 : 0;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

BOOL sc_is_card_inserted()
{
    pthread_mutex_lock(&g_sc_reader_mutex);
    LONG rv = (g_sc_reader_state & SCARD_STATE_PRESENT) ? 1 : 0;
    pthread_mutex_unlock(&g_sc_reader_mutex);
    return rv;
}

LPSTR sc_reader_name()
{
    return g_sc_reader_name;
}
