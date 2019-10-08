/**
 * 
 */


#include "scard.h"

typedef enum {
    STATE_INITIAL,
    STATE_CHECK_READER,
    STATE_WAIT_READER,
    STATE_CHECK_CARD,
    STATE_WAIT_CARD,
    STATE_CONNECT,
    STATE_DISCONNECT,
    STATE_IDENTIFY,
    STATE_READ,
    STATE_SET_PIN,
    STATE_PRESENT_PIN,
    STATE_WAIT_USER,
    STATE_UPDATE,
    STATE_IDLE,
    STATE_ERROR,
    NUM_STATES } state_t;

typedef struct instance_data instance_data_t;
typedef state_t state_func_t( instance_data_t *data );

state_t do_state_initial( instance_data_t *data );
state_t do_state_check_reader( instance_data_t *data );
state_t do_state_wait_reader( instance_data_t *data );
state_t do_state_check_card( instance_data_t *data );
state_t do_state_wait_card( instance_data_t *data );
state_t do_state_connect( instance_data_t *data );
state_t do_state_disconnect( instance_data_t *data );
state_t do_state_identify( instance_data_t *data );
state_t do_state_read( instance_data_t *data );
state_t do_state_set_pin( instance_data_t *data );
state_t do_state_present_pin( instance_data_t *data );
state_t do_state_wait_user( instance_data_t *data );
state_t do_state_update( instance_data_t *data );
state_t do_state_idle( instance_data_t *data );
state_t do_state_error( instance_data_t *data );

state_func_t* const state_table[ NUM_STATES ] = {
    do_state_initial,
    do_state_check_reader,
    do_state_wait_reader,
    do_state_check_card,
    do_state_wait_card,
    do_state_connect,
    do_state_disconnect,
    do_state_identify,
    do_state_read,
    do_state_set_pin,
    do_state_present_pin,
    do_state_wait_user,
    do_state_update,
    do_state_idle,
    do_state_error
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

    // user card data
    uint32_t user_magic;
    uint32_t user_id;
    uint32_t user_total;
    uint32_t user_value;

    // card readiness
    bool card_ready;

    // update data
    uint32_t new_value;
    uint32_t new_id;
    bool do_update;
};

static SCARDCONTEXT _context = 0;
static pthread_t _thread_id = 0;
static bool _thread_run = true;
static SCARDHANDLE _card = 0;
static instance_data_t _data = {0};

static state_t run_state( state_t cur_state, instance_data_t *data )
{
    state_t new_state = state_table[cur_state](data);
    return new_state;
};

static void forget_card(instance_data_t *data)
{
    TRC("clearing user info..\n");
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

        cur_state = run_state(cur_state, &_data);

        if (! _thread_run) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    DBG("destroying CONTEXT 0x%08lX\n", _context);
    scard_destroy_context(&_context);

    return 0;
}

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

void scard_user_thread_stop()
{
    // thread will exit
    _thread_run = false;
    scard_cancel_wait(_context);
    pthread_join(_thread_id, NULL);
    DBG("Destroyed user thread\n");
}

void scard_cancel_wait(const SCARDCONTEXT context)
{
    // cancel waiting SCardEstablishContext() inside the thread
    if (context) {
        LONG rv = SCardCancel(context);
        CHECK("SCardCancel", rv);
    }
    DBG("Canceled wait for a change..\n");
}




state_t do_state_initial( instance_data_t *data )
{
    TRC(">>>\n");
    scard_detect_reader(_context);
    return STATE_CHECK_READER;
}

state_t do_state_check_reader( instance_data_t *data )
{
    TRC(">>>\n");
    if (! scard_reader_presence()) {
        return STATE_WAIT_READER;
    }
    return STATE_CHECK_CARD;
}

state_t do_state_wait_reader( instance_data_t *data )
{
    TRC(">>>\n");
    DBG("NO READER\n");
    DBG("waiting for reader..\n");
    scard_reset_reader_state();
    scard_reset_card_state();
    scard_wait_for_reader(_context, INFINITE);
    // this point is reached if state has changed or user canceled the wait
    return STATE_INITIAL;
}

state_t do_state_check_card( instance_data_t *data )
{
    TRC(">>>\n");
    DBG("READER %s\n", scard_reader_name());
    DBG("probing for card..\n");
    scard_wait_for_card(_context, 1);
    if (! scard_card_presence()) {
        return STATE_WAIT_CARD;
    }
    return STATE_CONNECT;
}

state_t do_state_wait_card( instance_data_t *data )
{
    TRC(">>>\n");
    DBG("NO CARD!\n");
    DBG("waiting for card insert..\n");
    scard_reset_card_state();
    scard_wait_for_card(_context, INFINITE);
    // this point is reached if state has changed or user canceled the wait
    return STATE_INITIAL;
}

state_t do_state_connect( instance_data_t *data )
{
    TRC(">>>\n");
    if (! scard_connect_card(_context, &_card)) {
        return STATE_INITIAL;
    }
    return STATE_IDENTIFY;
}

state_t do_state_disconnect( instance_data_t *data )
{
    TRC(">>>\n");
    forget_card(data);
    scard_disconnect_card(&_card);
    return STATE_INITIAL;
}

state_t do_state_identify( instance_data_t *data )
{
    TRC(">>>\n");
    if (! scard_get_reader_info(_card)) {
        return STATE_DISCONNECT;
    }
    if (! scard_select_memory_card(_card)) {
        return STATE_DISCONNECT;
    }
    data->pin_retries = 0xFF;
    data->pin_code1 = data->pin_code2 = data->pin_code3 = 0xFF;
    if (! scard_get_error_counter(_card, &data->pin_code1, &data->pin_code2, &data->pin_code3, &data->pin_retries)) {
        return STATE_DISCONNECT;
    }
    return STATE_READ;
}

state_t do_state_read( instance_data_t *data )
{
    TRC(">>>\n");
    BYTE bytes[USER_AREA_LENGTH];
    if (! scard_read_user_data(_card, USER_AREA_ADDRESS, bytes, USER_AREA_LENGTH)) {
        return STATE_DISCONNECT;
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
        return STATE_SET_PIN;
    }

    data->card_ready = true;
    return STATE_PRESENT_PIN;
}

state_t do_state_set_pin( instance_data_t *data )
{
    TRC(">>>\n");

    // UNTESTED !!!!

    // use default PIN here!!!
    data->pin_retries = 0xFF;
    if (! scard_present_pin(_card, 0xFF, 0xFF, 0xFF, &data->pin_retries)) {
        return STATE_ERROR;
    }

    // use our PIN here!!!
    if (! scard_change_pin(_card, SC_PIN_CODE_BYTE_1, SC_PIN_CODE_BYTE_2, SC_PIN_CODE_BYTE_3)) {
        return STATE_ERROR;
    }
    DBG("Card PIN updated!\n");

    // reset the user values to defaults
    data->user_value = 0;
    data->user_total = 0;
    data->user_magic = SC_MAGIC_VALUE;
    data->user_id = SC_REGULAR_ID;
    // provide new values for update state
    data->new_id = SC_REGULAR_ID;
    data->new_value = 0;

    // perform initialization of the blank card now
    return STATE_UPDATE;
}

state_t do_state_present_pin( instance_data_t *data )
{
    TRC(">>>\n");
    data->pin_retries = 0xFF;
    if (! scard_present_pin(_card, SC_PIN_CODE_BYTE_1, SC_PIN_CODE_BYTE_2, SC_PIN_CODE_BYTE_3, &data->pin_retries)) {
        return STATE_ERROR;
    }
    return STATE_WAIT_USER;
}

state_t do_state_wait_user( instance_data_t *data )
{
    DBG("waiting for user UPDATE..\n");
    scard_wait_for_card(_context, INFINITE);
    // this point is reached if state has changed or user canceled the wait

    if (! data->do_update) {
        return STATE_DISCONNECT;
    }

    // clear flag; perform update only once
    data->do_update = false;
    return STATE_UPDATE;
}

state_t do_state_update( instance_data_t *data )
{
    TRC(">>>\n");

    uint32_t value = 0;
    if (data->new_id == SC_REGULAR_ID) {
        // add new value to remaining user value
        value = data->user_value + data->new_value;
    }

    // always use latest magic value!
    uint32_t magic = SC_MAGIC_VALUE;
    // 4x 32-bit integer in order: magic, ID, total, value
    BYTE bytes[USER_AREA_LENGTH] = {0};
    *(uint32_t *)&bytes[0] = magic;
    *(uint32_t *)&bytes[4] = data->new_id;
    // set value and total to be equal
    *(uint32_t *)&bytes[8] = value;
    *(uint32_t *)&bytes[12] = value;
    printf("new MAGIC %u\n", magic);
    printf("new ID    %u\n", data->new_id);
    printf("new TOTAL %u\n", value);
    printf("new VALUE %u\n", value);
    for (int i = 0; i < USER_AREA_LENGTH; i++) {
        printf("%02X ", *(bytes + i));
    }
    printf("\n");

    if (! scard_write_card(_card, USER_AREA_ADDRESS, bytes, USER_AREA_LENGTH)) {
        return STATE_ERROR;
    }
    DBG("Card updated, new value/total %u!\n", value);

    // force re-connect of the card, and re-read
    return STATE_DISCONNECT;
}

state_t do_state_idle( instance_data_t *data )
{
    TRC(">>>\n");

    DBG("waiting for change..\n");
    scard_wait_for_card(_context, INFINITE);
    // this point is reached if state has changed or user canceled the wait
    return STATE_INITIAL;
}

state_t do_state_error( instance_data_t *data )
{
    TRC(">>>\n");
    
    ERR("ERROR ERROR ERROR\n");
    // debug
    sleep(10);
    // debug

    if (! scard_card_presence()) {
        return STATE_INITIAL;
    }
    return STATE_ERROR;
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

void update_card(uint32_t value, uint32_t id)
{
    _data.new_value = value;
    _data.new_id = id;
    _data.do_update = true;
    // cancel wait and perform update
    scard_cancel_wait(_context);
}

bool is_card_ready()
{
    return _data.card_ready;
}
