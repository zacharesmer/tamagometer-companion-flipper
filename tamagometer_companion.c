/**
 * @file tamagometer_companion.c
 *
 * This application will display a text box with some scrollable text in it.
 * Press the Back key to exit the application.
 * It will also add the command "tamagometer" to the CLI for use with
 * https://zacharesmer.github.io/tamagometer/
 */

#include <furi.h>

#include <gui/gui.h>
#include <gui/modules/text_box.h>
#include <gui/view_holder.h>

#include <cli/cli_registry.h>
#include <furi_hal_infrared.h>
#include <infrared.h>
#include <infrared_transmit.h>
#include <infrared_worker.h>

#include <api_lock.h>

// borrowed from infrared_common_i.h
#define MATCH_TIMING(x, v, delta) (((x) < ((v) + (delta))) && ((x) > ((v) - (delta))))

// just get this to compile until they bring back RECORD_CLI
#define RECORD_CLI "cli"

static struct {
    bool command_decoded;
    bool timed_out;
    FuriApiLock cli_lock;
} app_state;

typedef struct {
    uint32_t header_mark;
    uint32_t header_mark_tolerance;
    uint32_t header_space;
    uint32_t header_space_tolerance;
    uint32_t data_mark;
    uint32_t data_mark_tolerance;
    uint32_t data_0_space;
    uint32_t data_0_space_tolerance;
    uint32_t data_1_space;
    uint32_t data_1_space_tolerance;
    uint32_t ending_mark;
    uint32_t ending_mark_tolerance;
} DecoderStates;

// all in micro-seconds
DecoderStates decoder_states = {
    .header_mark = 9600,
    .header_mark_tolerance = 2000,
    .header_space = 5000,
    .header_space_tolerance = 1500,
    .data_mark = 550,
    .data_mark_tolerance = 300, // max 850
    .data_0_space = 600,
    .data_0_space_tolerance = 400, // max 1000
    .data_1_space = 1500,
    .data_1_space_tolerance = 500, // match the min long gap to max short gap: 1000
    .ending_mark = 1100,
    .ending_mark_tolerance = 250, // match the min to max of data mark: 850
};

// This function will be called when the user presses the Back button.
static void back_button_callback(void* context) {
    // If a signal happens to be processing, wait for that to finish?
    FuriApiLock exit_lock = context;
    // Unlock the exit lock, thus enabling the app to exit.
    api_lock_unlock(exit_lock);
}

// take in a signal from the IR worker, and decode it into a 160 bit tamagotchi
// bit-string and store that in the provided buffer. Return true if success,
// false if it was not a decodable message.
//
// This function does not check the checksum for validity.
bool decode_signal_to_tamabits(InfraredWorkerSignal* received_signal, unsigned char* data_bits) {
    furi_assert(received_signal);
    // Get the timings from the recorded signal
    const uint32_t* timings;
    size_t timings_cnt;
    infrared_worker_get_raw_signal(received_signal, &timings, &timings_cnt);
    // Check if there are at least 323 timings, otherwise the message won't fit
    // and there's no possible way it's valid
    if(timings_cnt < 323) {
        return false;
    }
    // Check first 2 values to see if they're a valid preamble
    if(!(MATCH_TIMING(
             timings[0], decoder_states.header_mark, decoder_states.header_mark_tolerance) &&
         MATCH_TIMING(
             timings[1], decoder_states.header_space, decoder_states.header_space_tolerance))) {
        return false;
    }
    // Decode the next 160 pairs of bits, and store the result in data_bits
    size_t timing = 2;
    for(size_t data_bit = 0; data_bit < 160; data_bit++) {
        if(MATCH_TIMING(
               timings[timing], decoder_states.data_mark, decoder_states.data_mark_tolerance)) {
            if(MATCH_TIMING(
                   timings[timing + 1],
                   decoder_states.data_0_space,
                   decoder_states.data_0_space_tolerance)) {
                // It's a 0, store it
                data_bits[data_bit] = '0';
            } else if(MATCH_TIMING(
                          timings[timing + 1],
                          decoder_states.data_1_space,
                          decoder_states.data_1_space_tolerance)) {
                // It's a 1, store it
                data_bits[data_bit] = '1';
            } else {
                // It's not a valid 1 or 0, give up
                return false;
            }
        }
        timing += 2;
    }
    // I could check that the last mark is the end mark length (longer than a data
    // bit mark) but if we got 160 data bits that's good enough
    return true;
}

static void signal_received_callback(void* pipe, InfraredWorkerSignal* received_signal) {
    // todo: set processing_started flag
    furi_assert(received_signal);
    unsigned char tamabits[160];

    if(decode_signal_to_tamabits(received_signal, tamabits)) {
        // print out the signal
        // cli_write(cli, (uint8_t*)tamabits, 160);
        FURI_LOG_I("TEST", "I saw a signal!!!!");
        pipe_send(pipe, (unsigned char*)"[PICO]", 6);
        pipe_send(pipe, tamabits, 160);
        pipe_send(pipe, (unsigned char*)"[END]", 6);
        app_state.command_decoded = true;

    } else {
        // Do nothing I guess
        printf("Invalid signal received");
    }
    // // todo: set processing finished flag
}

static void timed_out_callback(void* arg) {
    UNUSED(arg);
    app_state.timed_out = true;
}

static void listen(void* context) {
    // set a timeout so the command will exit after 1 second
    FuriTimer* timer = furi_timer_alloc(timed_out_callback, FuriTimerTypeOnce, context);
    furi_timer_start(timer, furi_ms_to_ticks(1000));
    // furi_timer_restart(timer, furi_ms_to_ticks(1000));

    InfraredWorker* worker = infrared_worker_alloc();
    infrared_worker_rx_set_received_signal_callback(worker, signal_received_callback, context);
    infrared_worker_rx_start(worker);
    // default timeout value is 150,000 us, I need it shorter.
    furi_hal_infrared_async_rx_set_timeout(
        decoder_states.header_space + decoder_states.header_space_tolerance);

    // printf("Receiving %s INFRARED...\r\nPress Ctrl+C to abort\r\n", "RAW");
    while(!(app_state.command_decoded || app_state.timed_out || cli_is_pipe_broken_or_is_etx_next_char(context))) {
        furi_delay_ms(1);
    }

    // TODO: worry about the race condition where the timer times out while the
    // signal received callback is running and processing the signal. Could add a
    // flag that's set when a command starts processing and then make it wait
    // until that's done. If it's successful, do nothing because it will print the
    // decoded signal. If it was unsuccessful, print timed out message. There is
    // still a possibility that the signal is being received while the timer times
    // out. What happens then? At best it will get lost, at worst the callback
    // will be called and cause a null pointer dereference. Hmph.

    if(app_state.timed_out) {
        printf("[PICO]timed out[END]");
    }

    infrared_worker_rx_stop(worker);
    infrared_worker_free(worker);
    furi_timer_stop(timer);
    furi_timer_free(timer);
}

static bool tamabits_to_timings(char* bitstring, uint32_t* timings) {
    // check if bitstring is 160 chars
    if(strlen(bitstring) != 160) {
        return false;
    }
    timings[0] = decoder_states.header_mark;
    timings[1] = decoder_states.header_space;
    size_t t = 2;
    for(size_t i = 0; i < 160; i++) {
        timings[t] = decoder_states.data_mark;
        if(bitstring[i] == '0') {
            timings[t + 1] = decoder_states.data_0_space;
        } else if(bitstring[i] == '1') {
            timings[t + 1] = decoder_states.data_1_space;
        } else {
            return false;
        }
        t += 2;
    }
    timings[t] = decoder_states.ending_mark;
    return true;
}

static void send(char* bitstring) {
    // 2 timings for preamble, 320 for bits, 1 for ending mark
    uint32_t timings[2 + 320 + 1];
    if(tamabits_to_timings(bitstring, timings)) {
        infrared_send_raw(timings, 323, true);
    }
}

static void tamagometer_start_cli(PipeSide* pipe, FuriString* args, void* context) {
    UNUSED(context);
    // Acquire the cli_lock so that the GUI part of the app will wait to exit
    // if the CLI is still running something. This should hopefully reduce the
    // number of null pointer dereferences on exit
    api_lock_relock(app_state.cli_lock);
    FURI_LOG_I("TEST", "CLI ran...");

    app_state.command_decoded = false;
    app_state.timed_out = false;

    const char* args_string = furi_string_get_cstr(args);
    char bitstring[161];
    if(sscanf(args_string, "send%s", bitstring)) {
        // send the bitstring
        send(bitstring);
    } else if(strcmp(args_string, "listen") == 0) {
        // listen
        listen(pipe);
    } else {
        printf("Arguments: \"%s\"\n", args_string);
        printf("Invalid argument(s).\n");
    }
    api_lock_unlock(app_state.cli_lock);
    return;
}

int32_t tamagometer_companion(void* arg) {
    UNUSED(arg);

    // Create a lock that will be held any time the CLI command runs. It should be unlocked to start.
    // This is used to wait for it to exit the GUI part of the app and free everything
    app_state.cli_lock = api_lock_alloc_locked();
    api_lock_unlock(app_state.cli_lock);

    // Add the CLI command that the website will use to do stuff.
    // It will be removed when the GUI program exits.
    CliRegistry* cli = furi_record_open(RECORD_CLI);
    FURI_LOG_I("TEST", "Adding command to CLI...");
    cli_registry_add_command(cli, "tamagometer", CliCommandFlagParallelSafe, tamagometer_start_cli, NULL);
    furi_record_close(RECORD_CLI);

    // Access the GUI API instance.
    Gui* gui = furi_record_open(RECORD_GUI);
    // Create a TextBox view. The Gui object only accepts
    // ViewPort instances, so we will need to address that later.
    TextBox* text_box = text_box_alloc();
    // Set some text so that the text box is not empty.
    text_box_set_text(
        text_box,
        "Connect to\n"
        "zacharesmer.github.io/tamagometer\n\n"
        "Press \"Back\" to exit.");

    // Create a ViewHolder instance. It will serve as an adapter to convert
    // between the View type provided by the TextBox view and the ViewPort type
    // that the GUI can actually display.
    ViewHolder* view_holder = view_holder_alloc();
    // Let the GUI know about this ViewHolder instance.
    view_holder_attach_to_gui(view_holder, gui);
    // Set the view that we want to display.
    view_holder_set_view(view_holder, text_box_get_view(text_box));

    // The part below is not really related to this example, but is necessary for
    // it to function. We need to somehow stall the application thread so that the
    // view stays on the screen (otherwise the app will just exit and won't
    // display anything) and at the same time we need a way to quit out of the
    // application.

    // In this example, a simple FuriApiLock instance is used. A real-world
    // application is likely to have some kind of event handling loop here
    // instead. (see the ViewDispatcher example or one of FuriEventLoop examples
    // for that).

    // Create a pre-locked FuriApiLock instance.
    FuriApiLock exit_lock = api_lock_alloc_locked();
    // Set a Back event callback for the ViewHolder instance. It will be called
    // when the user presses the Back button. We pass the exit lock instance as
    // the context to be able to access it inside the callback function.
    view_holder_set_back_callback(view_holder, back_button_callback, exit_lock);

    // This call will block the application thread from running until the exit
    // lock gets unlocked somehow (the only way it can happen in this example is
    // via the back callback).
    api_lock_wait_unlock_and_free(exit_lock);

    // The back key has been pressed, which unlocked the exit lock. The
    // application is about to exit.

    // Remove the CLI command so it can't be used again
    CliRegistry* cli2 = furi_record_open(RECORD_CLI);
    FURI_LOG_I("TEST", "Deleting command from CLI...");
    cli_registry_delete_command(cli2, "tamagometer");
    furi_record_close(RECORD_CLI);
    // Wait for the CLI command to exit if it is running.
    api_lock_wait_unlock_and_free(app_state.cli_lock);

    // The view must be removed from a ViewHolder instance before deleting it.
    view_holder_set_view(view_holder, NULL);
    // Delete everything to prevent memory leaks.
    view_holder_free(view_holder);
    text_box_free(text_box);
    // End access to the GUI API.
    furi_record_close(RECORD_GUI);

    return 0;
}
