/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

#ifndef _PICO_PDM_MICROPHONE_H_
#define _PICO_PDM_MICROPHONE_H_

#include "../src/OpenPDM2PCM/OpenPDMFilter.h"

#include "hardware/pio.h"

typedef void (*pdm_samples_ready_handler_t)(void);



struct pdm_microphone_config {
    uint gpio_data;
    uint gpio_clk;
    PIO pio;
    uint pio_sm;
    uint sample_rate;
    uint sample_buffer_size;
};
#define PDM_DECIMATION       64
#define PDM_RAW_BUFFER_COUNT 2
struct _pdm_mic{
    struct pdm_microphone_config config;
    int dma_channel;
    uint8_t* raw_buffer[PDM_RAW_BUFFER_COUNT];
    volatile int raw_buffer_write_index;
    volatile int raw_buffer_read_index;
    uint raw_buffer_size;
    uint dma_irq;
    TPDMFilter_InitStruct filter;
    uint16_t filter_volume;
    pdm_samples_ready_handler_t samples_ready_handler;
};
extern struct _pdm_mic pdm_mic[3];


int pdm_microphone_init(const struct pdm_microphone_config* config, int mic_num);
void pdm_microphone_deinit(int mic_num);

int pdm_microphone_start();
void pdm_microphone_stop();

void pdm_microphone_set_samples_ready_handler(pdm_samples_ready_handler_t handler,int mic_num);
void pdm_microphone_set_filter_max_volume(uint8_t max_volume,int mic_num);
void pdm_microphone_set_filter_gain(uint8_t gain,int mic_num);
void pdm_microphone_set_filter_volume(uint16_t volume,int mic_num);

int pdm_microphone_read(int16_t* buffer, size_t samples,int mic_num);
extern void pdm_dma_handler();

#endif
