// SPDX-License-Identifier: GPL-2.0-only
/*
 * helper functions for Asus Xonar cards
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

#include <linux/delay.h>
#include <sound/core.h>
#include <sound/control.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include "main.h"
#include "oxygen_regs.h"


#define GPIO_CS53x1_M_MASK	0x000c
#define GPIO_CS53x1_M_SINGLE	0x0000
#define GPIO_CS53x1_M_DOUBLE	0x0004
#define GPIO_CS53x1_M_QUAD	0x0008

/**
 * Enable output of the card
 */
void xonar_enable_output(struct xonar *chip)
{
	struct xonar *data = chip;
    // set GPIO enable_output bit as the input to make set possible
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, data->output_enable_bit);
	// sleep to make sure second command works well
	msleep(data->anti_pop_delay);
	// enable output bit on GPIO
	oxygen_set_bits16(chip, OXYGEN_GPIO_DATA, data->output_enable_bit);
}


/**
 * Disable output of the card
 */
void xonar_disable_output(struct xonar *chip)
{
	struct xonar *data = chip;
    // disable output pin on GPIO
	oxygen_clear_bits16(chip, OXYGEN_GPIO_DATA, data->output_enable_bit);
}

/**
 * Read the external power state on GPI change and make proper action (no real action)
 */
void xonar_ext_power_gpio_changed(struct xonar *chip)
{
	struct xonar *data = chip;
	u8 has_power;

	has_power = !!(xonar_read8(chip, data->ext_power_reg)
		       & data->ext_power_bit);
	if (has_power != data->has_power) {
		data->has_power = has_power;
		if (has_power) {
			dev_notice(chip->card->dev, "power restored\n");
		} else {
			dev_crit(chip->card->dev,
				   "Hey! Don't unplug the power cable!\n");
			// comment from kernel
			// I think that this situation doesn't matter in this project
			/* TODO: stop PCMs */
		}
	}
}

/**
 * Set the initial external power state. In normal situation it should have external power.
 * @param chip
 */
void xonar_init_ext_power(struct xonar *chip)
{
	struct xonar *data = chip;

	oxygen_set_bits8(chip, data->ext_power_int_reg,
			 data->ext_power_bit);
	chip->interrupt_mask |= OXYGEN_INT_GPIO;
	chip->gpio_changed = xonar_ext_power_gpio_changed;
	data->has_power = !!(xonar_read8(chip, data->ext_power_reg)
			     & data->ext_power_bit);
	// print external power state
	printk(KERN_ERR "External power state: %d", data->has_power);
}

/**
 * Initialize CS5361 DAC (not used as it is capture DAC)
 * @param chip
 */
void xonar_init_cs53x1(struct xonar *chip)
{
	oxygen_set_bits16(chip, OXYGEN_GPIO_CONTROL, GPIO_CS53x1_M_MASK);
	// only set single/double/quad speed
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
			      GPIO_CS53x1_M_SINGLE, GPIO_CS53x1_M_MASK);
}

/**
 * Update capture DAC parameters, not used.
 */
void xonar_set_cs53x1_params(struct xonar *chip,
			     struct snd_pcm_hw_params *params)
{
	unsigned int value;

	// set only single/double/quad parameter
	if (params_rate(params) <= 54000)
		value = GPIO_CS53x1_M_SINGLE;
	else if (params_rate(params) <= 108000)
		value = GPIO_CS53x1_M_DOUBLE;
	else
		value = GPIO_CS53x1_M_QUAD;
	oxygen_write16_masked(chip, OXYGEN_GPIO_DATA,
			      value, GPIO_CS53x1_M_MASK);
}

/**
 * Get status of the front panel switch
 */
int xonar_gpio_bit_switch_get(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct xonar *chip = ctl->private_data;
	u16 bit = ctl->private_value;
	bool invert = ctl->private_value & XONAR_GPIO_BIT_INVERT;

	value->value.integer.value[0] =
		!!(xonar_read16(chip, OXYGEN_GPIO_DATA) & bit) ^ invert;
	return 0;
}

/**
 * Set front panel switch
 */
int xonar_gpio_bit_switch_put(struct snd_kcontrol *ctl,
			      struct snd_ctl_elem_value *value)
{
	struct xonar *chip = ctl->private_data;
	u16 bit = ctl->private_value;
	bool invert = ctl->private_value & XONAR_GPIO_BIT_INVERT;
	u16 old_bits, new_bits;
	int changed;

	spin_lock_irq(&chip->lock);
	old_bits = xonar_read16(chip, OXYGEN_GPIO_DATA);
	if (!!value->value.integer.value[0] ^ invert)
		new_bits = old_bits | bit;
	else
		new_bits = old_bits & ~bit;
	changed = new_bits != old_bits;
	// write to the hardware
	if (changed)
		oxygen_write16(chip, OXYGEN_GPIO_DATA, new_bits);
	spin_unlock_irq(&chip->lock);
	return changed;
}
