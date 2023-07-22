/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 * This examples creates a USB Microphone device using the TinyUSB
 * library and captures data from a PDM microphone using a sample
 * rate of 16 kHz, to be sent the to PC.
 * 
 * The USB microphone code is based on the TinyUSB audio_test example.
 * 
 * https://github.com/hathach/tinyusb/tree/master/examples/device/audio_test
 */

#include "pico/pdm_microphone.h"
#include "usb_microphone.h"

#define SAMPLE_RATE ((CFG_TUD_AUDIO_EP_SZ_IN / 2) - 1) * 1000
#define SAMPLE_BUFFER_SIZE ((CFG_TUD_AUDIO_EP_SZ_IN/2) - 1)
#define PEAK_DETECTOR_HEIGHT 40
#define AMPLITUDE_THRESH 8000


// configuration
const struct pdm_microphone_config config0 = {
  .gpio_data = 2,
  .gpio_clk = 3,
  .pio = pio0,
  .pio_sm = 0,
  .sample_rate = SAMPLE_RATE,
  .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

const struct pdm_microphone_config config1 = {
  .gpio_data = 4,
  .gpio_clk = 5,
  .pio = pio0,
  .pio_sm = 1,
  .sample_rate = SAMPLE_RATE,
  .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};

const struct pdm_microphone_config config2 = {
  .gpio_data = 6,
  .gpio_clk = 7,
  .pio = pio0,
  .pio_sm = 2,
  .sample_rate = SAMPLE_RATE,
  .sample_buffer_size = SAMPLE_BUFFER_SIZE,
};
void software_reset()
{
    watchdog_enable(1, 1);
    while(1);
}
void on_pdm_samples_ready(int mic_num);

// variables
void on_pdm_samples_ready0(){
    on_pdm_samples_ready(0);
}
void on_pdm_samples_ready1(){
    on_pdm_samples_ready(1);
}
void on_pdm_samples_ready2(){
    on_pdm_samples_ready(2);
}
// callback functions
void on_usb_microphone_tx_ready();
// turn on the clock for three state machines
void pdm_microphone_data_enable(PIO pio) { 
    check_pio_param(pio);
    // pio_sm_set_enabled(pio,sm3,true);
    // pio_sm_set_enabled(pio,sm2,true);
    // pio_sm_set_enabled(pio,sm,true);
    pio->ctrl |= 0b0111;
}
void core1_entry() {
    while (1) {
      while (1) {
        // run the USB microphone task continuously
        usb_microphone_task();
      }
    } // end while(1)
}
int main(void)
{
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
    pdm_microphone_data_enable(pio0);

  // initialize the USB microphone interface
  usb_microphone_init();
  usb_microphone_set_tx_ready_handler(on_usb_microphone_tx_ready);
  multicore_launch_core1(core1_entry);
  while (1) {
    // run the USB microphone task continuously
    // usb_microphone_task();
    tight_loop_contents();
  }

  return 0;
}
int16_t sample_buffer[3][SAMPLE_BUFFER_SIZE];
volatile int samples_read[3] = {0,0,0};
void on_pdm_samples_ready(int mic_num)
{
    // callback from library when all the samples in the library
    // internal sample buffer are ready for reading 
    // printf("SAMPLES %d\n",mic_num);
    samples_read[mic_num] = pdm_microphone_read(sample_buffer[mic_num], SAMPLE_BUFFER_SIZE, mic_num);
}
void on_usb_microphone_tx_ready()
{
  // Callback from TinyUSB library when all data is ready
  // to be transmitted.
  //
  // Write local buffer to the USB microphone
  usb_microphone_write(sample_buffer[2], sizeof(sample_buffer[0]));
}
