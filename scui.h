/**
 * 
 */

#ifndef SC_H_
#define SC_H_

bool sc_init();
void sc_destroy();

// reader & card detect thread
// bool sc_detect_thread_start();
// void sc_detect_thread_stop();

// card access thread
// bool sc_access_thread_start();
// void sc_access_thread_stop();

// card requests (handled by the card access thread)
bool sc_is_request_handled();
bool sc_request_connect();
bool sc_request_disconnect();
bool sc_request_reader_info();
bool sc_request_select_card();
bool sc_request_read_card();
bool sc_request_error_counter();
bool sc_request_present_pin();
bool sc_request_change_pin();
bool sc_request_write_card();

bool sc_is_reader_attached();
bool sc_is_card_inserted();
char *sc_get_reader_name();
bool sc_is_card_connected();
bool sc_is_card_ready();
void sc_identify_card();
void sc_forget_card();
void sc_get_user_data(uint32_t *magic, uint32_t *id, uint32_t *value, uint32_t *total);
void sc_set_user_data(bool want_admin, uint32_t new_value);

#endif // SC_H_
