
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

// formated logging
#define _LOG(x, ...) { \
    fprintf(stderr, "[%s] %s():%d : ", x, __func__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); \
    }
#define TRC(...) { _LOG("TRC", __VA_ARGS__) }
#define DBG(...) { _LOG("DBG", __VA_ARGS__) }
#define INF(...) { _LOG("INF", __VA_ARGS__) }
#define ERR(...) { _LOG("ERR", __VA_ARGS__) }

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
static pthread_t g_reader_thread = 0;
static pthread_mutex_t g_reader_mutex = PTHREAD_MUTEX_INITIALIZER;

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

#if 0
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
#endif

/**
 * Try to get the reader name.
 * 
 * If reader is attached then the global variable shall be updated
 * with reader name, otherwise the global reader will be reset, indicating
 * that there is no reader present.
 *
 * If present, reader name shall be 'ACS ACR38U-CCID 00 00' (under Linux)
 * of '???' (under Windows).
 *
 * Note that the code does not expect more than one reader in the system!
 */
static void reader_probe()
{
    LONG rv;
    DWORD dwReaders = MAX_READERNAME;
    BYTE mszReaders[MAX_READERNAME] = {0};
    LPSTR mszGroups = NULL;

    rv = SCardListReaders(g_scard_context, mszGroups, (LPSTR)&mszReaders, &dwReaders);
    // DBG("RV = 0x%08lX\n", rv);
    if (rv == SCARD_S_SUCCESS) {
        // reader arrived
        DBG("reader arrived ..\n");
    } else if (rv == SCARD_E_NO_READERS_AVAILABLE) {
        // reader left
        DBG("reader left ..\n");
    }

    // save reader name (NULL if not detected)
    pthread_mutex_lock(&g_reader_mutex);
    strncpy(g_reader_name, (LPCSTR)mszReaders, MAX_READERNAME);
    INF("reader name '%s'\n", g_reader_name);
    pthread_mutex_unlock(&g_reader_mutex);
}

/**
 * Thread routine which waits for change in reader state. 
 * @param  ptr NULL
 * @return     ???
 */
static void *reader_detect(void *ptr)
{
    LONG rv;
    SCARD_READERSTATE rgReaderStates[1] = {0};

    TRC("Enter\n");

    // try to get the reader name before waiting for notification
    // to arrive; detects already connected reader 
    reader_probe();

    while (1) {
        TRC("loop, waiting for reader to arrive or leave ..\n");
        (void)fflush(stdout);
        rgReaderStates[0].szReader = "\\\\?PnP?\\Notification";
        rgReaderStates[0].dwCurrentState = SCARD_STATE_UNAWARE;
        rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

        // SCardGetStatusChange() will exit on reader to arrival or departure
        rv = SCardGetStatusChange(g_scard_context, INFINITE, rgReaderStates, 1);
        CHECK("1 SCardGetStatusChange", rv);
        DBG("1 RV = 0x%08lX\n", rv);
        DBG("1 dwEventState:   0x%08lX\n", rgReaderStates[0].dwEventState);
        DBG("1 dwCurrentState: 0x%08lX\n", rgReaderStates[0].dwCurrentState);

        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        if (rv == SCARD_E_CANCELLED) {
            TRC("stopping thread ..\n");
            break;
        }

        // try to get the reader name now that the notification arrived
        reader_probe();

        // if reader is present check for the card
        if (strlen(g_reader_name)) {
            TRC("checking for card presence ..\n");
            // assume card is not present, and ask for status update!
            // if the card is in, the SCardGetStatusChange() will immediately exit,
            // if the card is out, SCardGetStatusChange() will block infinitely until card arrives..
            // use SCardCancel() to break out of infinite wait
            DBG("reader name %s\n", g_reader_name);
            rgReaderStates[0].szReader = g_reader_name;
            rgReaderStates[0].dwCurrentState = SCARD_STATE_EMPTY;
            rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;
            DBG("2a dwEventState:   0x%08lX\n", rgReaderStates[0].dwEventState);
            DBG("2a dwCurrentState: 0x%08lX\n", rgReaderStates[0].dwCurrentState);
            rv = SCardGetStatusChange(g_scard_context, INFINITE, rgReaderStates, 1);
            CHECK("3 SCardGetStatusChange", rv);
            DBG("2b RV = 0x%08lX\n", rv);
            DBG("2b dwEventState:   0x%08lX\n", rgReaderStates[0].dwEventState);
            DBG("2b dwCurrentState: 0x%08lX\n", rgReaderStates[0].dwCurrentState);
#if 0
            // XXX: should use rv return status code?
            // in case of disconnected reader event state remains unaltered
            if (rgReaderStates[0].dwEventState == SCARD_STATE_UNAWARE) {
                // qCritical() << "reader status not received, reader removed?";
                // reader_forget();
                // card_forget();
                // return false;
            } else {

                if (rv == SCARD_S_SUCCESS) {
                    //reader responded, check the event state for card status
                    if (rgReaderStates[0].dwEventState & SCARD_STATE_PRESENT) {
                        DBG("card present!\n");
                    } else if (rgReaderStates[0].dwEventState & SCARD_STATE_EMPTY) {
                        DBG("card absent!\n");
                    } else {
                        ERR("unexpected event state?!?!\n");
                    }
                }
            }
#endif
        }
    }

    TRC("Leave 0\n");
    return 0;
}

#if 0
static void *card_access(void *ptr)
{
    LONG rv;
    SCARD_READERSTATE rgReaderStates[1];
    // BYTE reader_name[MAX_READERNAME] = {0};

    TRC("Enter\n");

    while (1) {
        TRC("waiting for reader to arrive..\n");
        pthread_mutex_lock(&g_reader_mutex);
        pthread_cond_wait(&g_card_cond, &g_reader_mutex);
        pthread_mutex_unlock(&g_reader_mutex);
        TRC("reader arrived!!!\n");
        TRC("loop, waiting for card to arrive or leave ..\n");
        rgReaderStates[0].szReader = g_reader_name;
        rgReaderStates[0].dwCurrentState = SCARD_STATE_UNAWARE;
        rgReaderStates[0].dwEventState = SCARD_STATE_UNAWARE;

        DBG("reader name %s\n", g_reader_name);
        rv = SCardGetStatusChange(g_scard_context, 0, rgReaderStates, 1);
        CHECK("SCardGetStatusChange", rv);
        DBG("RV = 0x%08lX\n", rv);
        DBG("dwEventState: 0x%08lX\n", rgReaderStates[0].dwEventState);
        // XXX: should use rv return status code?
        // in case of disconnected reader event state remains unaltered
        if (rgReaderStates[0].dwEventState == SCARD_STATE_UNAWARE) {
            // qCritical() << "reader status not received, reader removed?";
            // reader_forget();
            // card_forget();
            // return false;
        } else {

            if (rv == SCARD_S_SUCCESS) {
                //reader responded, check the event state for card status
                if (rgReaderStates[0].dwEventState & SCARD_STATE_PRESENT) {
                    DBG("card present!\n");
                } else if (rgReaderStates[0].dwEventState & SCARD_STATE_EMPTY) {
                    DBG("card absent!\n");
                } else {
                    ERR("unexpected event state?!?!\n");
                }
            }
        }
    }

    TRC("Leave 0\n");
    return 0;
}
#endif

/**
 * Start the reader detection thread.
 * @return 0 - success, 1 - failure
 */
LONG scard_reader_start_thread()
{
    TRC("Enter\n");

    int rv = pthread_create(&g_reader_thread, NULL, reader_detect, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        TRC("Leave 1\n");
        return 1;
    }

    TRC("Leave 0\n");
    return 0;
}

/**
 * Stop the reader detection thread.
 * @return 0 - success, 1 - failure
 */
LONG scard_reader_stop_thread()
{
    TRC("Enter\n");

    // cancel waiting SCardEstablishContext() inside the thread
    if (g_scard_context) {
        LONG rv = SCardCancel(g_scard_context);
        CHECK("SCardCancel", rv);
    }
    pthread_join(g_reader_thread, NULL);

    TRC("Leave 0\n");
    return 0;
}
