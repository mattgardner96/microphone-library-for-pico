/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * This examples captures data from a PDM microphone using a sample
 * rate of 8 kHz and prints the sample values over the USB serial
 * connection.
 */
#define PICO_DEFAULT_UART_BAUD_RATE 2000000
#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/pdm_microphone.h"
#include "hardware/watchdog.h"
#include "tusb.h"

#define SAMPLE_RATE 40000
#define SAMPLE_BUFFER_SIZE SAMPLE_RATE/100
#define PEAK_DETECTOR_HEIGHT 40
#define AMPLITUDE_THRESH 8000

// configuration
// first microphone config
const struct pdm_microphone_config config0 = {
    // GPIO pin for the PDM DAT signal
    .gpio_data = 2,

    // GPIO pin for the PDM CLK signal
    .gpio_clk = 3,

    // PIO instance to use
    .pio = pio0,

    // PIO State Machine instance to use
    .pio_sm = 0,

    // sample rate in Hz
    .sample_rate = SAMPLE_RATE,//just at 5MHz PDM clock I think

    // number of samples to buffer
    .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

// second microphone config
const struct pdm_microphone_config config1 = {
    // GPIO pin for the PDM DAT signal
    .gpio_data = 4,
    // GPIO pin for the PDM CLK signal
    .gpio_clk = 5,
    // PIO instance to use
    .pio = pio0,
    // PIO State Machine instance to use
    .pio_sm = 1,
    // sample rate in Hz
    .sample_rate = SAMPLE_RATE,//just at 5MHz PDM clock I think
    // number of samples to buffer
    .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

// third microphone config
const struct pdm_microphone_config config2 = {
    // GPIO pin for the PDM DAT signal
    .gpio_data = 6,
    // GPIO pin for the PDM CLK signal
    .gpio_clk = 7,
    // PIO instance to use
    .pio = pio0,
    // PIO State Machine instance to use
    .pio_sm = 2,
    // sample rate in Hz
    .sample_rate = SAMPLE_RATE,//just at 5MHz PDM clock I think
    // number of samples to buffer
    .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

// get time from internal clock in us
uint64_t get_time_us()
{
    return to_us_since_boot(get_absolute_time());
}

// variables
int16_t sample_buffer[3][SAMPLE_BUFFER_SIZE];
volatile int samples_read[3] = {0,0,0};
void on_pdm_samples_ready(int mic_num)
{
    // callback from library when all the samples in the library
    // internal sample buffer are ready for reading 
    // printf("SAMPLES %d\n",mic_num);
    samples_read[mic_num] = pdm_microphone_read(sample_buffer[mic_num], SAMPLE_BUFFER_SIZE, mic_num);
}
void on_pdm_samples_ready0(){
    on_pdm_samples_ready(0);
}
void on_pdm_samples_ready1(){
    on_pdm_samples_ready(1);
}
void on_pdm_samples_ready2(){
    on_pdm_samples_ready(2);
}


void software_reset()
{
    watchdog_enable(1, 1);
    while(1);
}

// turn off the clock for three state machines
void pdm_microphone_data_disable(PIO pio, uint sm, uint sm2, uint sm3) {
    pio_sm_set_enabled(pio, sm, false);
    pio_sm_set_enabled(pio, sm2, false);
    pio_sm_set_enabled(pio, sm3, false);
} 
#define bool_to_bit(x) ((uint)!!(x))
// turn on the clock for three state machines
void pdm_microphone_data_enable(PIO pio, uint sm, uint sm2, uint sm3) { 
    check_pio_param(pio);
    check_sm_param(sm);
    check_sm_param(sm2);
    check_sm_param(sm3);
    pio_sm_set_enabled(pio,sm3,true);
    pio_sm_set_enabled(pio,sm2,true);
    pio_sm_set_enabled(pio,sm,true);
}


void process_samples(int mic_num){
    static uint64_t global_samples[3] = {0};

    bool printed = false;
    uint32_t peak_sample_num = 0;
    int num_over = 0;
    uint64_t time_now = 0;
    uint64_t time_last = 0;

    int count = 0;
    // store and clear the samples read from the callback
    int sample_count = samples_read[mic_num];
    samples_read[mic_num] = 0;
    // printf("Got %d samples on %d\n",sample_count, mic_num);

    // loop through any new collected samples
    int highest = 0;
    for (int i = 0; i < sample_count; i++) { // loop through all the samples in the buffer that we've collected
        global_samples[mic_num]+=1;
        // we stay in this if statement as long as large values are coming in consecutively
        if(abs(sample_buffer[mic_num][i]) > AMPLITUDE_THRESH){          // if this one sample is over the threshold...
            num_over++;                                                    // increment the number of consecutive samples over the threshold

            // convert ternary to if statement
            if(num_over>highest){                               // peak detector
                highest = num_over;
                peak_sample_num = global_samples[mic_num];               // only update the peak sample number if we have a new peak
                // printf("global_samples: %d\n",global_samples);           // DEBUG
            }
        }
        else { // if the sample is under the threshold
            num_over = 0; // reset the number of samples over the threshold 
            printed=false;
        }
    }

    // this is printing the peak reading over the threshold, which there's only one of per event
    if(highest>PEAK_DETECTOR_HEIGHT) {
        printf("\tBOUNCE: %d %"PRIu64" %d, %d\n",mic_num,time_now,highest,peak_sample_num);
    }

    // good for debugging the threshold settings
    if(highest)printf("%d best\n",highest);

    if(samples_read[mic_num] != 0){
        printf("BUFFER CORRUPTION DETECTED\n");
    }
}
void core1_entry() {
    while (1) {
        // wait for new samples
        for(int buffer_index = 0; buffer_index < 3; buffer_index++){
            if (samples_read[buffer_index] != 0){
                process_samples(buffer_index);
            }
        }
    } // end while(1)
}
int main( void )
{
    // initialize stdio and wait for USB CDC connect
    stdio_init_all();
    while (!tud_cdc_connected()) {
        tight_loop_contents();
    }

    printf("hello PDM microphone\n");
    if (pdm_microphone_init(&config0,0) < 0) {
        printf("PDM microphone 1 initialization failed!\n"); 
        software_reset();
    }

    // // initialize the second PDM microphone
    if (pdm_microphone_init(&config1,1) < 0) {
        printf("PDM microphone 2 initialization failed!\n");
        software_reset();
    }

    // initialize the third PDM microphone
    if (pdm_microphone_init(&config2,2) < 0) {
        printf("PDM microphone 3 initialization failed!\n");
        software_reset();
    }

    // set callback that is called when all the samples in the library
    // internal sample buffer are ready for reading
    pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready0,0);
    pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready1,1);
    pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready2,2);
    
    // start capturing data from the PDM microphone
    if (pdm_microphone_start(0) < 0) {
        printf("PDM microphone start failed!\n"); // this was fine
        software_reset();
    }
    if (pdm_microphone_start(1) < 0) {
        printf("PDM microphone start failed!\n"); // this was fine
        software_reset();
    }
    if (pdm_microphone_start(2) < 0) {
        printf("PDM microphone start failed!\n"); // this was fine
        software_reset();
        
    }
    multicore_launch_core1(core1_entry);
    pdm_microphone_data_enable(config0.pio,config0.pio_sm,config1.pio_sm,config2.pio_sm);

    while(1){tight_loop_contents();}

    return 0;
} // end main

