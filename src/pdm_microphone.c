/*
 * Copyright (c) 2021 Arm Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 * 
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/irq.h"



#include "pdm_microphone.pio.h"

#include "pico/pdm_microphone.h"

struct _pdm_mic pdm_mic[3];
void pdm_dma_handler();


int pdm_microphone_init(const struct pdm_microphone_config* config, int mic_num) {
    memset(&pdm_mic[mic_num], 0x00, sizeof(pdm_mic[mic_num]));
    memcpy(&pdm_mic[mic_num].config, config, sizeof(pdm_mic[mic_num].config));

    if (config->sample_buffer_size % (config->sample_rate / 1000)) {
        return -1;
    }

    pdm_mic[mic_num].raw_buffer_size = config->sample_buffer_size * (PDM_DECIMATION / 8);

    for (int i = 0; i < PDM_RAW_BUFFER_COUNT; i++) {
        pdm_mic[mic_num].raw_buffer[i] = malloc(pdm_mic[mic_num].raw_buffer_size);
        if (pdm_mic[mic_num].raw_buffer[i] == NULL) {
            pdm_microphone_deinit(mic_num);
            printf("FAILED MALLOC\n");
            while(1)tight_loop_contents();

            return -1;   
        }
    }

    pdm_mic[mic_num].dma_channel = dma_claim_unused_channel(true);
    if (pdm_mic[mic_num].dma_channel < 0) {
        pdm_microphone_deinit(mic_num);

        return -1;
    }

    printf("raw_buffer_size %d %d %d\n",pdm_mic[mic_num].raw_buffer_size,mic_num,pdm_mic[mic_num].dma_channel);

    uint pio_sm_offset = pio_add_program(config->pio, &pdm_microphone_data_program);

    float clk_div = clock_get_hz(clk_sys) / (config->sample_rate * PDM_DECIMATION * 4.0);

    pdm_microphone_data_init(
        config->pio,
        config->pio_sm,
        pio_sm_offset,
        clk_div,
        config->gpio_data,
        config->gpio_clk
    );
    dma_channel_config dma_channel_cfg = dma_channel_get_default_config(pdm_mic[mic_num].dma_channel);

    channel_config_set_transfer_data_size(&dma_channel_cfg, DMA_SIZE_8);
    channel_config_set_read_increment(&dma_channel_cfg, false);
    channel_config_set_write_increment(&dma_channel_cfg, true);
    channel_config_set_dreq(&dma_channel_cfg, pio_get_dreq(config->pio, config->pio_sm, false));
    pdm_mic[mic_num].dma_irq = DMA_IRQ_0;

    dma_channel_configure(
        pdm_mic[mic_num].dma_channel,
        &dma_channel_cfg,
        pdm_mic[mic_num].raw_buffer[0],
        &config->pio->rxf[config->pio_sm],
        pdm_mic[mic_num].raw_buffer_size,
        false
    );

    pdm_mic[mic_num].filter.Fs = config->sample_rate;
    pdm_mic[mic_num].filter.LP_HZ = config->sample_rate / 2;
    pdm_mic[mic_num].filter.HP_HZ = 10;
    pdm_mic[mic_num].filter.In_MicChannels = 1;
    pdm_mic[mic_num].filter.Out_MicChannels = 1;
    pdm_mic[mic_num].filter.Decimation = PDM_DECIMATION;
    pdm_mic[mic_num].filter.MaxVolume = 64;
    pdm_mic[mic_num].filter.Gain = 16;

    pdm_mic[mic_num].filter_volume = pdm_mic[mic_num].filter.MaxVolume;
}

void pdm_microphone_deinit(int mic_num) {
    for (int i = 0; i < PDM_RAW_BUFFER_COUNT; i++) {
        if (pdm_mic[mic_num].raw_buffer[i]) {
            free(pdm_mic[mic_num].raw_buffer[i]);

            pdm_mic[mic_num].raw_buffer[i] = NULL;
        }
    }

    if (pdm_mic[mic_num].dma_channel > -1) {
        dma_channel_unclaim(pdm_mic[mic_num].dma_channel);

        pdm_mic[mic_num].dma_channel = -1;
    }
}

int pdm_microphone_start(int mic_num) {
    irq_set_enabled(pdm_mic[mic_num].dma_irq, true);
    irq_set_exclusive_handler(pdm_mic[mic_num].dma_irq, pdm_dma_handler);
    dma_channel_set_irq0_enabled(pdm_mic[mic_num].dma_channel, true);

    Open_PDM_Filter_Init(&pdm_mic[mic_num].filter);

    // pio_sm_set_enabled(     // TODO all the sm stuff might not be duplicated?
    //     pdm_mic[mic_num].config.pio,
    //     pdm_mic[mic_num].config.pio_sm,
    //     true
    // );

    pdm_mic[mic_num].raw_buffer_write_index = 0;
    pdm_mic[mic_num].raw_buffer_read_index = 0;

    dma_channel_transfer_to_buffer_now(
        pdm_mic[mic_num].dma_channel,
        pdm_mic[mic_num].raw_buffer[0],
        pdm_mic[mic_num].raw_buffer_size
    );

    // pio_sm_set_enabled(     // TODO all the sm stuff might not be duplicated?
    //     pdm_mic[mic_num].config.pio,
    //     pdm_mic[mic_num].config.pio_sm,
    //     true
    // );
}

void pdm_microphone_stop(int mic_num) {    // TODO all the sm stuff might not be duplicated?
    pio_sm_set_enabled(
        pdm_mic[mic_num].config.pio,
        pdm_mic[mic_num].config.pio_sm,
        false
    );
    if(mic_num == 0){
        dma_channel_abort(pdm_mic[mic_num].dma_channel);
        dma_channel_set_irq0_enabled(pdm_mic[mic_num].dma_channel, false);
        irq_set_enabled(pdm_mic[mic_num].dma_irq, false);
    }
}

void pdm_dma_handler() {
    // clear IRQ
    int mic_num = 0;
    // printf("irq %d\n",dma_hw->ints0 );
    for(int i = 0; i < 3; i++){
        if(dma_hw->ints0 & (1 << pdm_mic[i].dma_channel)) {
            dma_hw->ints0 = (1u << pdm_mic[i].dma_channel);
            mic_num = i;
            break;
        }
    }
    


    // get the current buffer index
    pdm_mic[mic_num].raw_buffer_read_index = pdm_mic[mic_num].raw_buffer_write_index;
    // get the next capture index to send the dma to start
    pdm_mic[mic_num].raw_buffer_write_index = (pdm_mic[mic_num].raw_buffer_write_index + 1) % PDM_RAW_BUFFER_COUNT;

    // give the channel a new buffer to write to and re-trigger it
    dma_channel_transfer_to_buffer_now(
        pdm_mic[mic_num].dma_channel,
        pdm_mic[mic_num].raw_buffer[pdm_mic[mic_num].raw_buffer_write_index],
        pdm_mic[mic_num].raw_buffer_size
    );

    if (pdm_mic[mic_num].samples_ready_handler) {
        pdm_mic[mic_num].samples_ready_handler();
    }
}

void pdm_microphone_set_samples_ready_handler(pdm_samples_ready_handler_t handler,int mic_num) {
    pdm_mic[mic_num].samples_ready_handler = handler;
}

void pdm_microphone_set_filter_max_volume(uint8_t max_volume,int mic_num) {
    pdm_mic[mic_num].filter.MaxVolume = max_volume;
}

void pdm_microphone_set_filter_gain(uint8_t gain,int mic_num) {
    pdm_mic[mic_num].filter.Gain = gain;
}

void pdm_microphone_set_filter_volume(uint16_t volume,int mic_num) {
    pdm_mic[mic_num].filter_volume = volume;
}

int pdm_microphone_read(int16_t* buffer, size_t samples,int mic_num) {
    int filter_stride = (pdm_mic[mic_num].filter.Fs / 1000);
    samples = (samples / filter_stride) * filter_stride;
    if (samples > pdm_mic[mic_num].config.sample_buffer_size) {
        samples = pdm_mic[mic_num].config.sample_buffer_size;
    }
    if (pdm_mic[mic_num].raw_buffer_write_index == pdm_mic[mic_num].raw_buffer_read_index) {
        printf("DETECTED NULL READ %d\n",mic_num);
        return 0;
    }

    uint8_t* in = pdm_mic[mic_num].raw_buffer[pdm_mic[mic_num].raw_buffer_read_index];
    int16_t* out = buffer;

    pdm_mic[mic_num].raw_buffer_read_index++;

    for (int i = 0; i < samples; i += filter_stride) {
        Open_PDM_Filter_64(in, out, pdm_mic[mic_num].filter_volume, &pdm_mic[mic_num].filter);
        in += filter_stride * (PDM_DECIMATION / 8);
        out += filter_stride;
    }

    return samples;
}
