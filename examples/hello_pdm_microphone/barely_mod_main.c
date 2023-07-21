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
#define SAMPLES_OVER_THRESH 20
#define SAMPLES_THRESH 9000
// configuration
const struct pdm_microphone_config config = {
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


// get time from internal clock in us
uint64_t get_time_us()
{
    return to_us_since_boot(get_absolute_time());
}

// variables
int16_t sample_buffer[SAMPLE_BUFFER_SIZE];
volatile int samples_read = 0;

void on_pdm_samples_ready()
{
    // callback from library when all the samples in the library
    // internal sample buffer are ready for reading 
    samples_read = pdm_microphone_read(sample_buffer, SAMPLE_BUFFER_SIZE);
}
void software_reset()
{
    watchdog_enable(1, 1);
    while(1);
}

int main( void )
{
    // initialize stdio and wait for USB CDC connect
    stdio_init_all();
    while (!tud_cdc_connected()) {
        tight_loop_contents();
    }

    printf("hello PDM microphone\n");

    // initialize the PDM microphone
    if (pdm_microphone_init(&config) < 0) {
        printf("PDM microphone initialization failed!\n");
        software_reset();
    }

    // set callback that is called when all the samples in the library
    // internal sample buffer are ready for reading
    pdm_microphone_set_samples_ready_handler(on_pdm_samples_ready);
    
     // start capturing data from the PDM microphone
    if (pdm_microphone_start() < 0) {
        printf("PDM microphone start failed!\n");
        software_reset();
    }
    int num_over = 0;
    bool printed = false;
    while (1) {
        // wait for new samples
        while (samples_read == 0) { tight_loop_contents(); }

        // store and clear the samples read from the callback
        int sample_count = samples_read;
        samples_read = 0;
        // printf("Got %d samples\n",sample_count);
        // // loop through any new collected samples
        int highest = 0;
        for (int i = 0; i < sample_count; i++) {
            
            if(abs(sample_buffer[i]) > SAMPLES_THRESH){
                num_over++;
                if(num_over > SAMPLES_OVER_THRESH && !printed){
                    printf("%" PRIu64 ",GOT ONE %d\n",get_time_us(),i);
                    printed = true;
                }
                highest = num_over>highest?num_over:highest;
            }
            else{
                num_over = 0;
                printed=false;
            }
        }
        //good for debugging the threshold settings
        // if(highest)printf("%d best\n",highest);
        if(samples_read != 0){
            printf("BUFFER CORRUPTION DETECTED\n");
        }
    }

    return 0;
}
