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

#include "pico/stdlib.h"
#include "pico/pdm_microphone.h"
#include "tusb.h"
#define SAMPLE_RATE 80000
#define SAMPLE_BUFFER_SIZE SAMPLE_RATE/100
#define SAMPLES_OVER_THRESH 25
#define PEAK_DETECTOR_HEIGHT 20
#define AMPLITUDE_THRESH 10000
#define HOLDOFF_TIME_US 50000 // 2ms

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

// // second microphone config
// const struct pdm_microphone_config config2 = {
//     // GPIO pin for the PDM DAT signal
//     .gpio_data = 4,
//     // GPIO pin for the PDM CLK signal
//     .gpio_clk = 5,
//     // PIO instance to use
//     .pio = pio0,
//     // PIO State Machine instance to use
//     .pio_sm = 1,
//     // sample rate in Hz
//     .sample_rate = SAMPLE_RATE,//just at 5MHz PDM clock I think
//     // number of samples to buffer
//     .sample_buffer_size = SAMPLE_BUFFER_SIZE,
// };

// // third microphone config
// const struct pdm_microphone_config config3 = {
//     // GPIO pin for the PDM DAT signal
//     .gpio_data = 6,
//     // GPIO pin for the PDM CLK signal
//     .gpio_clk = 7,
//     // PIO instance to use
//     .pio = pio0,
//     // PIO State Machine instance to use
//     .pio_sm = 1,
//     // sample rate in Hz
//     .sample_rate = SAMPLE_RATE,//just at 5MHz PDM clock I think
//     // number of samples to buffer
//     .sample_buffer_size = SAMPLE_BUFFER_SIZE,
// };

// get time from internal clock in us
uint64_t get_time_us()
{
    return to_us_since_boot(get_absolute_time());
}

// variables
int16_t sample_buffer1[SAMPLE_BUFFER_SIZE];
volatile int samples_read = 0;

int thisMic = 0;        // which microphone we're using (starting with 0)

void on_pdm_samples_ready()
{
    // callback from library when all the samples in the library
    // internal sample buffer are ready for reading 
    samples_read = pdm_microphone_read(sample_buffer1, SAMPLE_BUFFER_SIZE, thisMic);
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

// turn on the clock for three state machines
void pdm_microphone_data_enable(PIO pio, uint sm, uint sm2, uint sm3) { 
    pio_sm_set_enabled(pio, sm, true);
    pio_sm_set_enabled(pio, sm2, true);
    pio_sm_set_enabled(pio, sm3, true);
}

int main( void )
{
    // initialize stdio and wait for USB CDC connect
    stdio_init_all();
    while (!tud_cdc_connected()) {
        tight_loop_contents();
    }

    printf("hello PDM microphone\n");

    // initialize the PDM microphone object
    if (pdm_microphone_init(&config0,thisMic) < 0) {
        printf("PDM microphone 1 initialization failed!\n"); 
        software_reset();
    }

    // // initialize the second PDM microphone
    // if (pdm_microphone_init(&config2) < 0) {
    //     printf("PDM microphone 2 initialization failed!\n");
    //     software_reset();
    // }

    // // initialize the third PDM microphone
    // if (pdm_microphone_init(&config3) < 0) {
    //     printf("PDM microphone 3 initialization failed!\n");
    //     software_reset();
    // }

    // set callback that is called when all the samples in the library
    // internal sample buffer are ready for reading
    pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready,thisMic);
    
     // start capturing data from the PDM microphone
    if (pdm_microphone_start(0) < 0) {
        printf("PDM microphone start failed!\n"); // this was fine
        software_reset();
    }

    int num_over = 0;
    bool printed = false;
    int global_samples = 0;
    uint32_t peak_sample_num = 0;

    uint64_t time_now = 0;
    uint64_t time_last = 0;

    int count = 0;

    while (1) {
        // wait for new samples
        while (samples_read == 0) { tight_loop_contents();}

        // store and clear the samples read from the callback
        int sample_count = samples_read;
        samples_read = 0;
        printf("Got %d samples\n",sample_count);

        // loop through any new collected samples
        int highest = 0;
        for (int i = 0; i < sample_count; i++) { // loop through all the samples in the buffer that we've collected
            global_samples++;                    
            
            // we stay in this if statement as long as large values are coming in consecutively
            if(abs(sample_buffer1[i]) > AMPLITUDE_THRESH){          // if this one sample is over the threshold...
                num_over++;                                                    // increment the number of consecutive samples over the threshold
                if(num_over > SAMPLES_OVER_THRESH && !printed){     // if we've been over the threshold for enough consecutive samples and haven't printed yet
                    // printf("%" PRIu64 ",GOT ONE %d\n",get_time_us(),i);
                    printed = true;
                }

                // convert ternary to if statement
                if(num_over>highest){                               // peak detector
                    highest = num_over;
                    peak_sample_num = global_samples;               // only update the peak sample number if we have a new peak
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
            
            // WE DETECTED!!!
            count++;
            time_now = get_time_us();

            // this is the holdoff time, so we don't print too many events (debouncer)
            if(time_now - time_last > HOLDOFF_TIME_US){
                printf("\tBOUNCE: %"PRIu64" %d, %d\n",time_now,highest,peak_sample_num);
                time_last = time_now;
            }
        }

        //good for debugging the threshold settings
        // if(highest)printf("%d best\n",highest);

        if(samples_read != 0){
            printf("BUFFER CORRUPTION DETECTED\n");
        }
    } // end while(1)

    return 0;
} // end main

