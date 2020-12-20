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
#define GPIO_D1_FRONT_PANEL	0x0002
#define GPIO_D1_MAGIC		0x00c0
#define GPIO_D1_INPUT_ROUTE	0x0100


#define I2C_DEVICE_CS4398	0x9e	/* 10011, AD1=1, AD0=1, /W=0 */
#define I2C_DEVICE_CS4362A	0x30	/* 001100, AD0=0, /W=0 */

/*
 * Write set values into the hardware registers.
 */
static void cs43xx_registers_init(struct xonar *chip);

/**
 * Initialize and set all needed hardware
 */
void xonar_dx_init(struct xonar *chip) {
    // XONAR DX
    struct xonar *data = chip;

    // only playback
    chip->device_config = PLAYBACK_0_TO_I2S;

    chip->dac_mclks = OXYGEN_MCLKS(256, 128, 128),
    chip->adc_mclks = OXYGEN_MCLKS(256, 128, 128),

    // format of bits as left-justified which is default
    chip->dac_i2s_format = OXYGEN_I2S_FORMAT_LJUST,
    chip->adc_i2s_format = OXYGEN_I2S_FORMAT_LJUST,

    // disable some oxygen actions
    chip->function_flags = 0;
    chip->misc_flags = 0;

    // number of channels in pcm and mixer controls
    chip->dac_channels_pcm = 8,
    chip->dac_channels_mixer = 8,
    // max and min vol level
    chip->dac_volume_min = 127 - 60,
    chip->dac_volume_max = 127,


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
    // reg[4] (mute control): low mute polarity, don't mute both channels and set auto-mute for PCM
    data->cs4398_regs[4] = CS4398_MUTEP_LOW |
                           /*CS4398_MUTE_B | CS4398_MUTE_A |*/ CS4398_PAMUTE;
    // reg[5] is volume control for channel A
    // volume is in 1/2 dB increments in range 0-(-127.5dB); 0=0dB, 255=-127.5dB
    data->cs4398_regs[5] = 0 * 2;  // 0dB
    // reg[6] is volume control for channel B
    data->cs4398_regs[6] = 0 * 2;
    // reg[7] (ramp and filter control): turn on ramp-down, damp-up, zero-cross and soft ramp.
    data->cs4398_regs[7] = CS4398_RMP_DN | CS4398_RMP_UP |
                           CS4398_ZERO_CROSS | CS4398_SOFT_RAMP;
    // SET REST OF THE OUTPUTS DAC TODO?
    data->cs4362a_regs[4] = CS4362A_RMP_DN | CS4362A_DEM_NONE;
    data->cs4362a_regs[6] = CS4362A_FM_SINGLE |
                            CS4362A_ATAPI_B_R | CS4362A_ATAPI_A_L;
    // don't mute all channels
    data->cs4362a_regs[7] = 0 /*| CS4362A_MUTE*/;
    data->cs4362a_regs[8] = 0 /*| CS4362A_MUTE*/;
    data->cs4362a_regs[9] = data->cs4362a_regs[6];
    data->cs4362a_regs[10] = 0 /*| CS4362A_MUTE*/;
    data->cs4362a_regs[11] = 0 /*| CS4362A_MUTE*/;
    data->cs4362a_regs[12] = data->cs4362a_regs[6];
    data->cs4362a_regs[13] = 0 /*| CS4362A_MUTE*/;
    data->cs4362a_regs[14] = 0 /*| CS4362A_MUTE*/;

    // probably set I2C output connection properly
    oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
                   OXYGEN_2WIRE_LENGTH_8 |
                   OXYGEN_2WIRE_INTERRUPT_MASK |
                   OXYGEN_2WIRE_SPEED_FAST);

    // write values from software registers into hardware registers
    cs43xx_registers_init(chip);

    // set proper bits as writeable
    oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL,
                      GPIO_D1_FRONT_PANEL |
                      GPIO_D1_MAGIC |
                      GPIO_D1_INPUT_ROUTE);
    // disable ?, front panel output and set input route to line-in
    oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA,
                        GPIO_D1_FRONT_PANEL | GPIO_D1_INPUT_ROUTE);

    // Input DAC?
    xonar_init_cs53x1(chip);
    // enable cards' output
    xonar_enable_output(chip);

    // add set DACs as components of the card
    snd_component_add(chip->card, "CS4398");
    snd_component_add(chip->card, "CS4362A");
    snd_component_add(chip->card, "CS5361");

    // set volume levels properly
    update_xonar_volume(chip);
    update_xonar_mute(chip);
}

static void cs4362a_write(struct xonar *chip, u8 reg, u8 value);
void xonar_dx_cleanup(struct xonar *chip)
{
    // disable output from the card
    xonar_disable_output(chip);
    // TODO check in sheets disable second DAC?
    cs4362a_write(chip, 0x01, CS4362A_PDN | CS4362A_CPEN);
    // OXYGEN things
    oxygen_clear_bits8(chip, OXYGEN_FUNCTION, OXYGEN_FUNCTION_RESET_CODEC);
}

void xonar_d1_resume(struct xonar *chip)
{
    oxygen_set_bits8(chip, OXYGEN_FUNCTION, OXYGEN_FUNCTION_RESET_CODEC);
    msleep(1);
    cs43xx_registers_init(chip);
    xonar_enable_output(chip);
}

static void cs4398_write_cached(struct xonar *chip, u8 reg, u8 value);
static void cs4362a_write_cached(struct xonar *chip, u8 reg, u8 value);
void set_cs43xx_params(struct xonar *chip, struct snd_pcm_hw_params *params)
{
    struct xonar *data = chip;
    u8 cs4398_fm, cs4362a_fm;

    // set single/double/quad speed of DAC sample rate
    if (params_rate(params) <= 50000) {
        cs4398_fm = CS4398_FM_SINGLE;
        cs4362a_fm = CS4362A_FM_SINGLE;
    } else if (params_rate(params) <= 100000) {
        cs4398_fm = CS4398_FM_DOUBLE;
        cs4362a_fm = CS4362A_FM_DOUBLE;
    } else {
        cs4398_fm = CS4398_FM_QUAD;
        cs4362a_fm = CS4362A_FM_QUAD;
    }
    cs4398_fm |= CS4398_DEM_NONE | CS4398_DIF_LJUST;
    cs4398_write_cached(chip, 2, cs4398_fm);
    cs4362a_fm |= data->cs4362a_regs[6] & ~CS4362A_FM_MASK;
    cs4362a_write_cached(chip, 6, cs4362a_fm);
    cs4362a_write_cached(chip, 12, cs4362a_fm);
    cs4362a_fm &= CS4362A_FM_MASK;
    cs4362a_fm |= data->cs4362a_regs[9] & ~CS4362A_FM_MASK;
    cs4362a_write_cached(chip, 9, cs4362a_fm);
}


// HARDWARE WRITES

static void cs4398_write(struct xonar *chip, u8 reg, u8 value)
{
	struct xonar *data = chip;

	oxygen_write_i2c(chip, I2C_DEVICE_CS4398, reg, value);
	if (reg < ARRAY_SIZE(data->cs4398_regs))
		data->cs4398_regs[reg] = value;
}

static void cs4398_write_cached(struct xonar *chip, u8 reg, u8 value)
{
	struct xonar *data = chip;

	if (value != data->cs4398_regs[reg])
		cs4398_write(chip, reg, value);
}

static void cs4362a_write(struct xonar *chip, u8 reg, u8 value)
{
	struct xonar *data = chip;

	oxygen_write_i2c(chip, I2C_DEVICE_CS4362A, reg, value);
	if (reg < ARRAY_SIZE(data->cs4362a_regs))
		data->cs4362a_regs[reg] = value;
}

static void cs4362a_write_cached(struct xonar *chip, u8 reg, u8 value)
{
	struct xonar *data = chip;

	if (value != data->cs4362a_regs[reg])
		cs4362a_write(chip, reg, value);
}

static void cs43xx_registers_init(struct xonar *chip)
{
	struct xonar *data = chip;
	unsigned int i;

	/* set CPEN (control port mode) and power down */
	cs4398_write(chip, 8, CS4398_CPEN | CS4398_PDN);
	cs4362a_write(chip, 0x01, CS4362A_PDN | CS4362A_CPEN);
	/* configure */
	cs4398_write(chip, 2, data->cs4398_regs[2]);
	cs4398_write(chip, 3, CS4398_ATAPI_B_R | CS4398_ATAPI_A_L);
	cs4398_write(chip, 4, data->cs4398_regs[4]);
	cs4398_write(chip, 5, data->cs4398_regs[5]);
	cs4398_write(chip, 6, data->cs4398_regs[6]);
	cs4398_write(chip, 7, data->cs4398_regs[7]);
	cs4362a_write(chip, 0x02, CS4362A_DIF_LJUST);
	cs4362a_write(chip, 0x03, CS4362A_MUTEC_6 | CS4362A_AMUTE |
		      CS4362A_RMP_UP | CS4362A_ZERO_CROSS | CS4362A_SOFT_RAMP);
	cs4362a_write(chip, 0x04, data->cs4362a_regs[0x04]);
	cs4362a_write(chip, 0x05, 0);
	for (i = 6; i <= 14; ++i)
		cs4362a_write(chip, i, data->cs4362a_regs[i]);
	/* clear power down */
	cs4398_write(chip, 8, CS4398_CPEN);
	cs4362a_write(chip, 0x01, CS4362A_CPEN);
}


// MIXER ACTIONS

void update_xonar_volume(struct xonar *chip)
{
    unsigned int i;
    u8 mute;

    // update volume on front panel
    cs4398_write_cached(chip, 5, (127 - chip->dac_volume[0]) * 2);
    cs4398_write_cached(chip, 6, (127 - chip->dac_volume[1]) * 2);

    printk(KERN_ERR "Front volume changed to: %d", chip->dac_volume[0]);

    // for the rest of the outs
    // check if should be muted and set mute flag if needed
    mute = chip->dac_mute ? CS4362A_MUTE : 0;
    // update sound vol/mute
    for (i = 0; i < 6; ++i)
        cs4362a_write_cached(chip, 7 + i + i / 2,
                             (127 - chip->dac_volume[2 + i]) | mute);
}

void update_xonar_mute(struct xonar *chip) {
    u8 reg, mute;
    int i;

    // normal "mute" register for front playback
    reg = CS4398_MUTEP_LOW | CS4398_PAMUTE;
    // if mute than add mute flags
    if (chip->dac_mute) {
        reg |= CS4398_MUTE_B | CS4398_MUTE_A;

        printk(KERN_ERR "DAC MUTED");
    } else {
        printk(KERN_ERR "DAC NOT MUTED");
    }
    // write created register val
    cs4398_write_cached(chip, 4, reg);

    // for the rest of the outs
    // check if should be muted and set mute flag if needed
    mute = chip->dac_mute ? CS4362A_MUTE : 0;
    // update sound vol/mute
    for (i = 0; i < 6; ++i)
        cs4362a_write_cached(chip, 7 + i + i / 2,
                             (127 - chip->dac_volume[2 + i]) | mute);
}

