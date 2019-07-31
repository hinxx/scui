#ifndef SC_UTIL_H_
#define SC_UTIL_H_

#ifdef WIN32
#undef UNICODE
// this is not defined in windows
#define MAX_READERNAME         128
#endif

#include <assert.h>
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

#define _UNUSED(arg) (void)arg;

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

#define SC_BUFFER_MAXLEN              256

LONG sc_create_context(PSCARDCONTEXT context);
LONG sc_destroy_context(PSCARDCONTEXT context);
LONG sc_detect_reader(const SCARDCONTEXT context);
LONG sc_wait_for_reader(const SCARDCONTEXT context, const ULONG timeout);
LONG sc_wait_for_card(const SCARDCONTEXT context, const ULONG timeout);
LONG sc_probe_for_card(const SCARDCONTEXT context);
LONG sc_wait_for_card_remove(const SCARDCONTEXT context);
LONG sc_wait_for_card_insert(const SCARDCONTEXT context);
bool sc_is_reader_attached();
bool sc_is_card_inserted();
LPSTR sc_get_reader_name();
LONG sc_connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle);
void sc_disconnect_card(PSCARDHANDLE handle);
bool sc_is_card_connected();
void sc_to_hex(LPBYTE data, ULONG len);
LONG sc_do_xfer(const SCARDHANDLE handle, const LPBYTE send_data, const ULONG send_len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_check_sw(const LPBYTE sw_data, const BYTE sw1, const BYTE sw2);
LONG sc_get_reader_info(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_select_memory_card(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_read_card(const SCARDHANDLE handle, BYTE address, BYTE len, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_get_error_counter(const SCARDHANDLE handle, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_present_pin(const SCARDHANDLE handle, LPBYTE pin, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);
LONG sc_change_pin(const SCARDHANDLE handle, LPBYTE pin, LPBYTE recv_data, ULONG *recv_len, LPBYTE sw_data);

#endif // SC_UTIL_H_
