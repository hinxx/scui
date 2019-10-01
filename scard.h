/**
 * 
 */

#ifndef SCARD_H_
#define SCARD_H_

#ifdef WIN32
#undef UNICODE
// this is not defined in windows
#define MAX_READERNAME         128
#endif

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

#define SC_MAX_READERNAME_LEN           128
#define SC_MAX_REQUEST_LEN              255
#define SC_MAX_FIRMWARE_LEN             10

#define SC_MAGIC_VALUE                  6970

#define SC_ADMIN_ID                     1
#define SC_REGULAR_ID                   9999

#define SC_PIN_CODE_BYTE_1              0xC0
#define SC_PIN_CODE_BYTE_2              0xDE
#define SC_PIN_CODE_BYTE_3              0xA5

// low level
bool scard_create_context(PSCARDCONTEXT context);
void scard_destroy_context(PSCARDCONTEXT context);
void scard_detect_reader(const SCARDCONTEXT context);
void scard_wait_for_reader(const SCARDCONTEXT context, const ULONG timeout);
void scard_wait_for_card(const SCARDCONTEXT context, const ULONG timeout);
bool scard_reader_presence();
bool scard_card_presence();
char *scard_reader_name();
void scard_reset_reader_state();
void scard_reset_card_state();
bool scard_connect_card(const SCARDCONTEXT context, PSCARDHANDLE handle);
void scard_disconnect_card(PSCARDHANDLE handle);
bool scard_get_reader_info(const SCARDHANDLE handle);
bool scard_select_memory_card(const SCARDHANDLE handle);
bool scard_get_error_counter(const SCARDHANDLE handle, LPBYTE pin1, LPBYTE pin2, LPBYTE pin3, LPBYTE pin_retries);
bool scard_read_user_data(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len);
bool scard_present_pin(const SCARDHANDLE handle, BYTE pin1, BYTE pin2, BYTE pin3, LPBYTE pin_retries);
bool scard_change_pin(const SCARDHANDLE handle, LPBYTE pin);
bool scard_write_card(const SCARDHANDLE handle, BYTE address, LPBYTE data, BYTE len);
void scard_cancel_wait(const SCARDCONTEXT context);

// detect
// bool scard_detect_thread_start();
// void scard_detect_thread_stop();

// user
bool scard_user_thread_start();
void scard_user_thread_stop();
unsigned scard_get_pin_retries();
unsigned scard_get_pin_user_magic();
unsigned scard_get_pin_user_id();
unsigned scard_get_pin_user_total();
unsigned scard_get_pin_user_value();
void update_card(uint32_t value, uint32_t id);

bool is_card_ready();

#endif // SCARD_H_
