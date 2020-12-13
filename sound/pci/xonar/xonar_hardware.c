//
// Created by Tomasz Piechocki on 13/12/2020.
//
// BASED ON:
// SPDX-License-Identifier: GPL-2.0-only
/*
 * card driver for models with CS4398/CS4362A DACs (Xonar D1/DX)
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

/*
 * Xonar D1/DX
 * -----------
 *
 * CMI8788:     playback card
 *
 *   I²C <-> CS4398 (addr 1001111) (front)
 *       <-> CS4362A (addr 0011000) (surround, center/LFE, back)
 *
 *   GPI 0 <- external power present (DX only)
 *
 *   GPIO 0 -> enable output to speakers
 *   GPIO 1 -> route output to front panel
 *   GPIO 2 -> M0 of CS5361
 *   GPIO 3 -> M1 of CS5361
 *   GPIO 6 -> ?
 *   GPIO 7 -> ?
 *   GPIO 8 -> route input jack to line-in (0) or mic-in (1)
 *
 * CM9780:      capture card
 *
 *   LINE_OUT -> input of ADC
 *
 *   AUX_IN  <- aux
 *   MIC_IN  <- mic
 *   FMIC_IN <- front mic
 *
 *   GPO 0 -> route line-in (0) or AC97 output (1) to CS5361 input
 *
 *   DOCS:
 *   CS4398: https://statics.cirrus.com/pubs/proDatasheet/CS4398_F2.pdf
 */

#include <linux/pci.h>
#include <linux/delay.h>
#include <sound/ac97_codec.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/tlv.h>

#include "main.h"
#include "oxygen_regs.h"

// enable output
#define GPIO_DX_OUTPUT_ENABLE	0x0001

static void xonar_dx_init(struct xonar *chip) {
    // XONAR DX
    struct xonar *data = chip;
    data->ext_power_reg = OXYGEN_GPI_DATA;
    data->ext_power_int_reg = OXYGEN_GPI_INTERRUPT_MASK;
    data->ext_power_bit = GPI_EXT_POWER;
    xonar_init_ext_power(chip);

    // SHARED WITH XONAR D1

    // delay to make sure that some hardware configuration works well, e.g. enable_output
    data->anti_pop_delay = 800;
    // enable output to speakers (not front panel)
    data->output_enable_bit = GPIO_DX_OUTPUT_ENABLE;

    // SET FRONT OUTPUT DAC
    // all needed flags explanation added in oxygen_regs.h

    // reg[1] (read-only) contains chip ID
    // reg[2]: set single speed mode (30-50kHz range), no de-emphasis filter and default data format (left-justified)
    data->cs4398_regs[2] =
            CS4398_FM_SINGLE | CS4398_DEM_NONE | CS4398_DIF_LJUST;
    // reg[3]: volume, mixing and inversion control - default values don't need to be changes
    // default is: independent sound levels for two channels and no invert signal polarity on them
    // reg[4] (mute control): low mute polarity, mute both channels and set auto-mute for PCM
    data->cs4398_regs[4] = CS4398_MUTEP_LOW |
                           CS4398_MUTE_B | CS4398_MUTE_A | CS4398_PAMUTE;
    // reg[5] is volume control for channel A
    // volume is in 1/2 dB increments in range 0-(-127.5dB); 0=0dB, 255=-127.5dB
    data->cs4398_regs[5] = 60 * 2;  // -60dB
    // reg[6] is volume control for channel B
    data->cs4398_regs[6] = 60 * 2;
    // reg[7] (ramp and filter control): turn on ramp-down, damp-up, zero-cross and soft ramp.
    data->cs4398_regs[7] = CS4398_RMP_DN | CS4398_RMP_UP |
                           CS4398_ZERO_CROSS | CS4398_SOFT_RAMP;
    // SET REST OF THE OUTPUTS DAC TODO?
    data->cs4362a_regs[4] = CS4362A_RMP_DN | CS4362A_DEM_NONE;
    data->cs4362a_regs[6] = CS4362A_FM_SINGLE |
                            CS4362A_ATAPI_B_R | CS4362A_ATAPI_A_L;
    data->cs4362a_regs[7] = 60 | CS4362A_MUTE;
    data->cs4362a_regs[8] = 60 | CS4362A_MUTE;
    data->cs4362a_regs[9] = data->cs4362a_regs[6];
    data->cs4362a_regs[10] = 60 | CS4362A_MUTE;
    data->cs4362a_regs[11] = 60 | CS4362A_MUTE;
    data->cs4362a_regs[12] = data->cs4362a_regs[6];
    data->cs4362a_regs[13] = 60 | CS4362A_MUTE;
    data->cs4362a_regs[14] = 60 | CS4362A_MUTE;

    oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
                   OXYGEN_2WIRE_LENGTH_8 |
                   OXYGEN_2WIRE_INTERRUPT_MASK |
                   OXYGEN_2WIRE_SPEED_FAST);

    cs43xx_registers_init(chip);

    oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
                      GPIO_D1_FRONT_PANEL |
                      GPIO_D1_MAGIC |
                      GPIO_D1_INPUT_ROUTE);
    oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA,
                        GPIO_D1_FRONT_PANEL | GPIO_D1_INPUT_ROUTE);

    xonar_init_cs53x1(chip);
    xonar_enable_output(chip);

    snd_component_add(chip->card, "CS4398");
    snd_component_add(chip->card, "CS4362A");
    snd_component_add(chip->card, "CS5361");
}


