
#include "scard_impl.h"

#define _UNUSED(arg) (void)arg;

// auto allocate some strings for some SCardXXX calls
#define USE_AUTOALLOCATE

#ifdef WIN32
static char *pcsc_stringify_error(LONG rv)
{
    static char out[20];
    sprintf_s(out, sizeof(out), "0x%08X", rv);
    return out;
}
#endif

// formated printout
#if 0
#define TRC(s) \
    fprintf(stderr, "[TRC] %s(): %s\n", __func__, s);
#define TRCI(s, i) \
    fprintf(stderr, "[TRC] %s(): %s (%d)\n", __func__, s, i);
#endif

#define TRC(...) fprintf(stderr, "[TRC] " __VA_ARGS__)
#define DBG(...) fprintf(stderr, "[DBG] " __VA_ARGS__)
#define INF(...) fprintf(stderr, "[INF] " __VA_ARGS__)
#define ERR(...) fprintf(stderr, "[ERR] " __VA_ARGS__)

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

static SCARDCONTEXT g_scard_context = 0;
static char g_reader_name[MAX_READERNAME] = {0};

LONG scard_init()
{
    TRC("Enter\n");
    // establish PC/SC Connection
    LONG rv = SCardEstablishContext(SCARD_SCOPE_USER, NULL, NULL, &g_scard_context);
    CHECK("SCardEstablishContext", rv);
    rv = SCardIsValidContext(g_scard_context);
    CHECK("SCardIsValidContext", rv);

    TRC("Leave 0\n");
    return 0;
}

LONG scard_destroy()
{
    TRC("Enter\n");
    if (g_scard_context) {
        LONG rv = SCardReleaseContext(g_scard_context);
        CHECK("SCardReleaseContext", rv);
        g_scard_context = 0;
    }

    TRC("Leave 0\n");
    return 0;
}

LONG scard_reader_find()
{
    SCARD_READERSTATE rgReaderStates[1];
    DWORD dwReaders = 0;
    char *mszReaders;
    char *mszGroups;
    DWORD i;
    int p, iReader;
    int iList[16] = {0};

    TRC("Enter\n");

    mszGroups = NULL;
    LONG rv = SCardListReaders(g_scard_context, mszGroups, NULL, &dwReaders);
    CHECK("SCardListReaders", rv);
    if (SCARD_E_NO_READERS_AVAILABLE == rv) {
        // qWarning() << "Please attach a USB CCID reader";
        (void)fflush(stdout);
        rgReaderStates[0].szReader = "\\\\?PnP?\\Notification";
        rgReaderStates[0].dwCurrentState = SCARD_STATE_EMPTY;

        // rv = SCardGetStatusChange(g_scard_context, INFINITE, rgReaderStates, 1);
        // with 200ms reschedule timeout in main UI this makes up about a 1 s
        // to wait for reader to appear..
        rv = SCardGetStatusChange(g_scard_context, 1, rgReaderStates, 1);
        CHECK("SCardGetStatusChange", rv);
    }

#ifdef USE_AUTOALLOCATE
    dwReaders = SCARD_AUTOALLOCATE;
    rv = SCardListReaders(g_scard_context, mszGroups, (LPSTR)&mszReaders, &dwReaders);
#else
    rv = SCardListReaders(g_scard_context, mszGroups, NULL, &dwReaders);
    RETURN("SCardListReaders", rv);

    mszReaders = calloc(dwReaders, sizeof(char));
    rv = SCardListReaders(g_scard_context, mszGroups, mszReaders, &dwReaders);
#endif
    RETURN("SCardListReaders", rv);

    // have to understand the multi-string here
    // dwReaders number of characters in mszReaders (including NULLs)
    p = 0;
    for (i = 0; i+1 < dwReaders; i++)
    {
        ++p;
        // qDebug() << "found reader: " << &mszReaders[i];
        DBG("found reader: %s\n", &mszReaders[i]);
        iList[p] = i;
        while (mszReaders[++i] != 0) ;
    }

    // just use the first reader present for now
    // XXX: improve by collecting list of readers and let user choose
    iReader = 1;
    char *reader = &mszReaders[iList[iReader]];
    // qDebug() << "selected reader: " << reader;
    DBG("selected reader: %s\n", reader);

    // just probe for card
    rgReaderStates[0].szReader = reader;
    rgReaderStates[0].dwCurrentState = SCARD_STATE_UNAWARE;
    // rv = SCardGetStatusChange(g_scard_context, INFINITE, rgReaderStates, 1);
    rv = SCardGetStatusChange(g_scard_context, 1, rgReaderStates, 1);
    RETURN("SCardGetStatusChange", rv);

    DBG("dwEventState: %lX\n", rgReaderStates[0].dwEventState);
    // save the reader name for other SCardXXX functions to use through mReader
    memset(g_reader_name, 0, MAX_READERNAME);
    strncpy(g_reader_name, reader, strlen(reader));
    INF("reader %s is connected\n", g_reader_name);

#ifdef SCARD_AUTOALLOCATE
        rv = SCardFreeMemory(g_scard_context, mszReaders);
        CHECK("SCardFreeMemory", rv);
#else
        free(mReader);
#endif

    TRC("Leave %ld\n", rv);
    return rv;
}

bool scard_reader_present()
{
    if (g_reader_name[0] == '\0') {
        if (scard_reader_find()) {
            TRC("Leave false\n");
            return false;
        }
    }

    SCARD_READERSTATE rgReaderStates[1];
    rgReaderStates[0].szReader = g_reader_name;
    rgReaderStates[0].dwCurrentState = SCARD_STATE_UNAWARE;
    rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

    // just probe for reader
    // rv = SCardGetStatusChange(g_scard_context, INFINITE, rgReaderStates, 1);
    LONG rv = SCardGetStatusChange(g_scard_context, 1, rgReaderStates, 1);
    CHECK("SCardGetStatusChange", rv);
    DBG("dwEventState: %lX\n", rgReaderStates[0].dwEventState);
    if (rgReaderStates[0].dwEventState == SCARD_STATE_UNAWARE) {
        ERR("reader status not received\n");
        // forget the reader
        memset(g_reader_name, 0, MAX_READERNAME);
        TRC("Leave false\n");
        return false;
    }
    INF("reader %s is connected\n", g_reader_name);

    TRC("Leave true\n");
    return true;
}
