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
        // sleep(2);

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

    // debug
    // sleep(1);
    // debug

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
    
    data->card_ready = true;

    if (data->user_magic == 0xFFFFFFFF) {
        // we have a new, vanilla, card
        return STATE_SET_PIN;
    }

    return STATE_PRESENT_PIN;
}

state_t do_state_set_pin( instance_data_t *data )
{
    TRC(">>>\n");
    // TODO !!!
    return STATE_ERROR;
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
