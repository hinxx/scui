
// #include "fsm.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

// // PC/SC lite
// #ifdef __APPLE__
// #include <PCSC/winscard.h>
// #include <PCSC/wintypes.h>
// #else
// #include <winscard.h>
// #endif

#include "helpers.h"
#include "scard.h"

static pthread_t fsm_thread_id = 0;
static bool fsm_thread_run = true;

typedef enum {
    STATE_INITIAL, STATE_IDLE,
    STATE_DETECT_READER, STATE_DETECT_CARD, STATE_READ_CARD, STATE_USER_WAIT,
    NUM_STATES } state_t;

typedef struct instance_data instance_data_t;
typedef state_t state_func_t( instance_data_t *data );

state_t do_state_initial( instance_data_t *data );
state_t do_state_idle( instance_data_t *data );
state_t do_state_detect_reader( instance_data_t *data );
state_t do_state_detect_card( instance_data_t *data );
state_t do_state_read_card( instance_data_t *data );
state_t do_state_user_wait( instance_data_t *data );

state_func_t* const state_table[ NUM_STATES ] = {
    do_state_initial, do_state_idle,
    do_state_detect_reader, do_state_detect_card, do_state_read_card, do_state_user_wait
};

// typedef void transition_func_t( instance_data_t *data );

// void do_initial_to_foo( instance_data_t *data );
// void do_foo_to_bar( instance_data_t *data );
// void do_bar_to_initial( instance_data_t *data );
// void do_bar_to_foo( instance_data_t *data );
// void do_bar_to_bar( instance_data_t *data );

// transition_func_t * const transition_table[ NUM_STATES ][ NUM_STATES ] = {
//     { NULL,              do_initial_to_foo, NULL },
//     { NULL,              NULL,              do_foo_to_bar },
//     { do_bar_to_initial, do_bar_to_foo,     do_bar_to_bar }
// };

#define MAX_IDLE_FRAMES     3

struct instance_data {
    unsigned in_idle;
    state_t after_idle;
};

static instance_data_t _data;
// static bool want_bar_state = false;
static int _fsm_loop = 0;

static state_t run_state( state_t cur_state, instance_data_t *data )
{
    state_t new_state = state_table[cur_state](data);

    // transition_func_t *transition = transition_table[cur_state][new_state];
    // if (transition) {
    //     transition(data);
    // }

    return new_state;
};



static void *fsm_thread_fnc(void *ptr)
{
    state_t cur_state = STATE_INITIAL;

    bool rv = scard_create_context();
    assert(rv != false);

    while (1) {
        _fsm_loop++;
        printf("\n");
        TRC("FSM loop #%d..\n", _fsm_loop);

        cur_state = run_state(cur_state, &_data);
        sleep(1);

        // exit the thread!
        if (! fsm_thread_run) {
            TRC("stopping thread ..\n");
            break;
        }
    }

    scard_destroy_context();

    return 0;
}

bool fsm_thread_start()
{
    int rv = pthread_create(&fsm_thread_id, NULL, fsm_thread_fnc, NULL);
    if (rv) {
        ERR("Error - pthread_create() return code: %d\n", rv);
        return false;
    }

    DBG("Created FSM thread\n");
    return true;
}

void fsm_thread_stop()
{
    // thread will exit
    fsm_thread_run = false;
    pthread_join(fsm_thread_id, NULL);
    DBG("Destroyed FSM thread\n");
}

state_t do_state_initial( instance_data_t *data )
{
    TRC("\n\n");
    return STATE_DETECT_READER;
}

state_t do_state_idle( instance_data_t *data )
{
    TRC("\n\n");
    data->in_idle--;
    if (data->in_idle) {
        return STATE_IDLE;
    }
    // assert(data->after_idle != NUM_STATES);
    // data->after_idle = NUM_STATES;
    return data->after_idle;
}

state_t do_state_detect_reader( instance_data_t *data )
{
    TRC("\n\n");
    scard_detect_reader();
    bool have_reader = scard_reader_presence();
    if (have_reader) {
        DBG("READER %s\n", scard_reader_name());
        return STATE_DETECT_CARD;
    } else {
        DBG("NO READER\n");
        DBG("waiting for reader..\n");
    }

    // return STATE_DETECT_READER;
    data->in_idle = MAX_IDLE_FRAMES;
    data->after_idle = STATE_DETECT_READER;
    return STATE_IDLE;
}

state_t do_state_detect_card( instance_data_t *data )
{
    TRC("\n\n");
    scard_probe_for_card();
    bool have_card = scard_card_presence();
    if (have_card) {
        DBG("CARD PRESENT..\n");
        // DBG("waiting for card remove..\n");
        // wait_for_card_remove(detect_context);
        return STATE_READ_CARD;
    } else {
        DBG("NO CARD!\n");
        DBG("waiting for card insert..\n");
        // reset_card_state();
        // wait_for_card_insert(detect_context);
    }
    
    data->in_idle = MAX_IDLE_FRAMES;
    data->after_idle = STATE_DETECT_READER;
    return STATE_IDLE;
}

state_t do_state_read_card( instance_data_t *data )
{
    TRC("\n\n");

    bool card_read = scard_card_identify();
    if (card_read) {
        return STATE_USER_WAIT;
    }

    data->in_idle = MAX_IDLE_FRAMES;
    data->after_idle = STATE_DETECT_READER;
    return STATE_IDLE;
}

state_t do_state_user_wait( instance_data_t *data )
{
    TRC("\n\n");
    // TODO!
    data->in_idle = MAX_IDLE_FRAMES;
    data->after_idle = STATE_USER_WAIT;
    return STATE_IDLE;
}
