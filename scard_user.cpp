/**
 * 
 */


#include "scard.h"

typedef enum {
    STATE_INITIAL,
    STATE_CARD_CONNECT,
    STATE_CARD_DISCONNECT,
    STATE_CARD_IDENTIFY,
    STATE_CARD_READ,
    STATE_CARD_SET_PIN,
    STATE_CARD_PRESENT_PIN,
    STATE_CARD_UPDATE,
    STATE_IDLE,
    NUM_STATES } state_t;

typedef struct instance_data instance_data_t;
typedef state_t state_func_t( instance_data_t *data );

state_t do_state_initial( instance_data_t *data );
state_t do_state_card_connect( instance_data_t *data );
state_t do_state_card_disconnect( instance_data_t *data );
state_t do_state_card_identify( instance_data_t *data );
state_t do_state_card_read( instance_data_t *data );
state_t do_state_card_set_pin( instance_data_t *data );
state_t do_state_card_present_pin( instance_data_t *data );
state_t do_state_card_update( instance_data_t *data );
state_t do_state_idle( instance_data_t *data );

state_func_t* const state_table[ NUM_STATES ] = {
    do_state_initial,
    do_state_card_connect,
    do_state_card_disconnect,
    do_state_card_identify,
    do_state_card_read,
    do_state_card_set_pin,
    do_state_card_present_pin,
    do_state_card_update,
    do_state_idle,
};

// user data start in smartcard memory
#define USER_AREA_ADDRESS       64
// user data is 16 bytes (4x 32-bit integer)
#define USER_AREA_LENGTH        16

struct instance_data {
    uint8_t pin_retries;
    uint8_t pin_code1;
    uint8_t pin_code2;
    uint8_t pin_code3;

    // user data
    uint32_t user_magic;
    uint32_t user_id;
    uint32_t user_total;
    uint32_t user_value;
};

static SCARDCONTEXT _context = 0;
static pthread_t _thread_id = 0;
static bool _thread_run = true;
static SCARDHANDLE _card = 0;
static instance_data_t _data = {0};



static state_t run_state( state_t cur_state, instance_data_t *data )
{
    state_t new_state = state_table[cur_state](data);

    // transition_func_t *transition = transition_table[cur_state][new_state];
    // if (transition) {
    //     transition(data);
    // }

    return new_state;
};

static void forget_card(instance_data_t *data)
{
    TRC("clearing user info..\n");
    // data->user_id = 0;
    // data->user_magic = 0;
    // data->user_value = 0;
    // data->user_total = 0;
    memset(data, 0, sizeof(instance_data_t));
}


static void *thread_fnc(void *ptr)
{
    state_t cur_state = STATE_INITIAL;
    unsigned fsm_loop = 0;

    bool rv = scard_create_context(&_context);
    DBG("created CONTEXT 0x%08lX\n", _context);
    assert(rv != false);

    while (1) {
        fsm_loop++;
        TRC("loop, #%d ..\n", fsm_loop);
        (void)fflush(stdout);

        // TRC("waiting for condition ..\n");
        // Lock mutex and then wait for signal to relase mutex
        // pthread_mutex_lock(&worker_mutex);
        // mutex gets unlocked if condition variable is signaled
        // pthread_cond_wait(&worker_condition, &worker_mutex);
        // this thread can now handle new request
        
        // reset handled request ID
        // l_req_id_handled = 0;
        // make local copies of the new request data
        // req_id = l_req_id;
        // request = worker_request;
        // req_len = l_req_len;
        // memcpy(req_data, l_req_data, l_req_len);
        // pthread_mutex_unlock(&worker_mutex);
        // TRC("condition met!\n");

        cur_state = run_state(cur_state, &_data);
        // sleep(2);

        if (! _thread_run) {
            TRC("stopping thread ..\n");
            break;
        }

        // invoke request handling function from this thread!
        // req_id_handled = sc_handle_request(req_id, req, req_data, req_len);
        // pthread_mutex_lock(&worker_mutex);
        // l_req_id_handled = req_id_handled; 
        // pthread_mutex_unlock(&worker_mutex);

        // if (request == SC_REQUEST_IDENTIFY_CARD) {
        //     process_identify();
        // } else if (request == SC_REQUEST_UPDATE_CARD) {
        //     process_update();
        // }

        // XXX not calling any SCardGetStatusChange() in this thread
        // if the wait was cancelled (i.e. exiting the app), exit the thread!
        // if (rv == SCARD_E_CANCELLED) {
        //     TRC("stopping thread ..\n");
        //     break;
        // }
    }

    DBG("destroying CONTEXT 0x%08lX\n", _context);
    scard_destroy_context(&_context);

    return 0;
}

/**
 * Start the reader and card state change thread.
 * @return true - success, false - failure
 */
bool scard_user_thread_start()
{
    int rv = pthread_create(&_thread_id, NULL, thread_fnc, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        return false;
    }

    DBG("Created user thread\n");
    return true;
}

/**
 * Stop the reader and card state change thread.
 * @return none
 */
void scard_user_thread_stop()
{
    // thread will exit
    _thread_run = false;

    // pthread_mutex_lock(&worker_mutex);
    // signal waiting thread by freeing the mutex
    // pthread_cond_signal(&worker_condition);
    // pthread_mutex_unlock(&worker_mutex);

    pthread_join(_thread_id, NULL);
    DBG("Destroyed user thread\n");
}




state_t do_state_initial( instance_data_t *data )
{
    TRC(">>>\n");

    sleep(1);

    if (! scard_reader_presence()) {
        return STATE_INITIAL;
    }
    if (! scard_card_presence()) {
        return STATE_INITIAL;
    }
    return STATE_CARD_CONNECT;
}

state_t do_state_card_connect( instance_data_t *data )
{
    TRC(">>>\n");
    if (! scard_connect_card(_context, &_card)) {
        return STATE_INITIAL;
    }
    return STATE_CARD_IDENTIFY;
}

state_t do_state_card_disconnect( instance_data_t *data )
{
    TRC(">>>\n");
    forget_card(data);
    scard_connect_card(_context, &_card);
    return STATE_INITIAL;
}

state_t do_state_card_identify( instance_data_t *data )
{
    TRC(">>>\n");
    if (! scard_get_reader_info(_card)) {
        return STATE_CARD_DISCONNECT;
    }
    if (! scard_select_memory_card(_card)) {
        return STATE_CARD_DISCONNECT;
    }
    data->pin_retries = 0;
    data->pin_code1 = data->pin_code2 = data->pin_code3 = 0;
    if (! scard_get_error_counter(_card, &data->pin_code1, &data->pin_code2, &data->pin_code3, &data->pin_retries)) {
        return STATE_CARD_DISCONNECT;
    }
    return STATE_CARD_READ;
}

state_t do_state_card_read( instance_data_t *data )
{
    TRC(">>>\n");
    
    BYTE bytes[USER_AREA_LENGTH];
    if (! scard_read_user_data(_card, USER_AREA_ADDRESS, bytes, USER_AREA_LENGTH)) {
        return STATE_CARD_DISCONNECT;
    }
    data->user_magic = *(uint32_t *)&bytes[0];
    data->user_id = *(uint32_t *)&bytes[4];
    data->user_total = *(uint32_t *)&bytes[8];
    data->user_value = *(uint32_t *)&bytes[12];
    DBG("MAGIC: %u\n", data->user_magic);
    DBG("CARD ID: %u\n", data->user_id);
    DBG("TOTAL: %u\n", data->user_total);
    DBG("VALUE: %u\n", data->user_value);
    
    if (data->user_magic == 0xFFFFFFFF) {
        // we have a new, vanilla, card
        return STATE_CARD_SET_PIN;
    }

    return STATE_CARD_PRESENT_PIN;
}

state_t do_state_card_set_pin( instance_data_t *data )
{
    TRC(">>>\n");
    return STATE_IDLE;
}

state_t do_state_card_present_pin( instance_data_t *data )
{
    TRC(">>>\n");
    return STATE_IDLE;
}

state_t do_state_card_update( instance_data_t *data )
{
    TRC(">>>\n");
    return STATE_IDLE;
}

state_t do_state_idle( instance_data_t *data )
{
    TRC(">>>\n");

    // debug    
    sleep(1);
    if (! scard_reader_presence()) {
        return STATE_CARD_DISCONNECT;
    }
    if (! scard_card_presence()) {
        return STATE_CARD_DISCONNECT;
    }
    // debug    

    return STATE_IDLE;
}



unsigned scard_get_pin_retries()
{
    return _data.pin_retries;
}
unsigned scard_get_pin_user_magic()
{
    return _data.user_magic;
}
unsigned scard_get_pin_user_id()
{
    return _data.user_id;
}
unsigned scard_get_pin_user_total()
{
    return _data.user_total;
}
unsigned scard_get_pin_user_value()
{
    return _data.user_value;
}






#if 0



static bool process_update()
{
    bool rv = false;

    rv = get_error_counter(worker_card);
    if (! rv) {
        return false;
    }

    // blank card has all bytes set to 0xFF, check user magic
    if (g_state.user_magic == 0xFFFFFFFF) {
        DBG("user magic is not set yet, need to present default PIN..\n");
        BYTE pin[3] = {0};
        // use default PIN here!!!
        pin[0] = 0xFF;
        pin[1] = 0xFF;
        pin[2] = 0xFF;
        rv = present_pin(worker_card, pin);
        if (! rv) {
            return false;
        }

        rv = get_error_counter(worker_card);
        if (! rv) {
            return false;
        }

        // use our PIN here!!!
        pin[0] = SC_PIN_CODE_BYTE_1;
        pin[1] = SC_PIN_CODE_BYTE_2;
        pin[2] = SC_PIN_CODE_BYTE_3;
        rv = change_pin(worker_card, pin);
        if (! rv) {
            return false;
        }

        // TODO
        // write ID, magic, value & total 0

    } else {
        DBG("user magic is set, need to present our PIN..\n");
        // before PIN is presented, returned PIN code from error check
        // is 0x00 0x00 0x00, afterwards is our PIN code; present code 
        // only once.
        if (! ((g_state.card_pin_code[0] == SC_PIN_CODE_BYTE_1)
            && (g_state.card_pin_code[1] == SC_PIN_CODE_BYTE_2)
            && (g_state.card_pin_code[2] == SC_PIN_CODE_BYTE_3))) {
            BYTE pin[3] = {0};
            // use our PIN here!!!
            pin[0] = SC_PIN_CODE_BYTE_1;
            pin[1] = SC_PIN_CODE_BYTE_2;
            pin[2] = SC_PIN_CODE_BYTE_3;
            rv = present_pin(worker_card, pin);
            if (! rv) {
                return false;
            }

            rv = get_error_counter(worker_card);
            if (! rv) {
                return false;
            }
        }

        uint32_t value = 0;
        // what type of card is requested?
        if (g_state.user_admin_card) {
            // set admin ID
            g_state.user_id = SC_ADMIN_ID;
        } else {
            // set regular ID
            g_state.user_id = SC_REGULAR_ID;
            // add new value to remaining user value
            value = g_state.user_value + g_state.user_add_value;
        }
        // set value and total to be equal
        g_state.user_value = value;
        g_state.user_total = value;
        // always use latest magic value!
        g_state.user_magic = SC_MAGIC_VALUE;

        // read from user data start
        BYTE address = 64;
        // read 16 bytes (4x 32-bit integers)
        BYTE length = 16;
        // data, 4x 32-bit integer in order: magic, ID, total, value
        BYTE data[16] = {0};
        data[0] =  (BYTE)(g_state.user_magic         & 0xFF);
        data[1] =  (BYTE)((g_state.user_magic >> 8)  & 0xFF);
        data[2] =  (BYTE)((g_state.user_magic >> 16) & 0xFF);
        data[3] =  (BYTE)((g_state.user_magic >> 24) & 0xFF);
        data[4] =  (BYTE)(g_state.user_id            & 0xFF);
        data[5] =  (BYTE)((g_state.user_id    >> 8)  & 0xFF);
        data[6] =  (BYTE)((g_state.user_id    >> 16) & 0xFF);
        data[7] =  (BYTE)((g_state.user_id    >> 24) & 0xFF);
        data[8] =  (BYTE)(g_state.user_total         & 0xFF);
        data[9] =  (BYTE)((g_state.user_total >> 8)  & 0xFF);
        data[10] = (BYTE)((g_state.user_total >> 16) & 0xFF);
        data[11] = (BYTE)((g_state.user_total >> 24) & 0xFF);
        data[12] = (BYTE)(g_state.user_value         & 0xFF);
        data[13] = (BYTE)((g_state.user_value >> 8)  & 0xFF);
        data[14] = (BYTE)((g_state.user_value >> 16) & 0xFF);
        data[15] = (BYTE)((g_state.user_value >> 24) & 0xFF);
        rv = write_card(worker_card, address, data, length);
        if (! rv) {
            return false;
        }
        DBG("Card updated, new value/total %u!\n", g_state.user_value);
    }

    return true;
}

/**
 * exported user API
 */

bool sc_init()
{
    TRC("Enter\n");

    bool rv = detect_thread_start();
    assert(rv != false);
    rv = worker_thread_start();
    assert(rv != false);

    TRC("Leave %d\n", rv);
    return rv;
}

void sc_destroy()
{
    TRC("Enter\n")
    
    detect_thread_stop();
    worker_thread_stop();

    TRC("Leave\n");
}

bool sc_is_reader_attached()
{
    return reader_presence();
}

bool sc_is_card_inserted()
{
    return card_presence();
}

bool sc_is_card_connected()
{
    return (worker_card != 0) ? true : false;
}

char *sc_get_reader_name()
{
    return reader_name();
}

void sc_identify_card()
{
    TRC("Enter\n")
    request_identify();
}

void sc_forget_card()
{
    TRC("Enter\n")
    disconnect_card(&worker_card);
}

void sc_get_user_data(uint32_t *magic, uint32_t *id, uint32_t *value, uint32_t *total)
{
    // TRC("Enter\n")
    *magic = g_state.user_magic;
    *id = g_state.user_id;
    *value = g_state.user_value;
    *total = g_state.user_total;
}

void sc_set_user_data(bool want_admin, uint32_t new_value)
{
    TRC("Enter\n")
    // do we want regular or admin card?
    g_state.user_admin_card = want_admin;
    // store newly bought credit
    g_state.user_add_value = new_value;
    DBG("creating %s card\n", g_state.user_admin_card ? "ADMIN" : "REGULAR");
    DBG("adding %d E to the card (if REGULAR)\n", g_state.user_add_value);
    request_update();
}

#endif
