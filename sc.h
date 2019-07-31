#ifndef SC_H_
#define SC_H_

#include "sc_util.h"
// #include "sc_thread.h"

bool sc_init();
void sc_destroy();

// reader & card detect thread
bool sc_detect_thread_start();
void sc_detect_thread_stop();

// card access thread
bool sc_access_thread_start();
void sc_access_thread_stop();

// card requests (handled by the card access thread)
bool sc_is_request_handled();
LONG sc_request_connect();
LONG sc_request_disconnect();
LONG sc_request_reader_info();

#endif // SC_H_
