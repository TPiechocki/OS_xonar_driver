//
// Created by Tomasz Piechocki on 19/12/2020.
//

#include <linux/mutex.h>
#include <sound/control.h>
#include <sound/core.h>

#include "main.h"

/**
 * Get information about possible volume settings.
 */
static int xonar_vol_info(struct snd_kcontrol *ctl,
                           struct snd_ctl_elem_info *info)
{
    struct xonar *chip = ctl->private_data;

    // information is of integer type
    info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    // card has 8 channels in DACs, so also the 8 vol controls (grouped in pairs)
    info->count = chip->dac_channels_mixer;
    // min and max volume
    info->value.integer.min = chip->dac_volume_min;
    info->value.integer.max = chip->dac_volume_max;
    return 0;
}

/**
 * Get current volume level
 */
static int xonar_vol_get(struct snd_kcontrol *ctl,
                          struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    unsigned int i;

    mutex_lock(&chip->mutex);
    // for every channel volume control
    for (i = 0; i < chip->dac_channels_mixer; ++i)
        // get the volume of that volume
        value->value.integer.value[i] = chip->dac_volume[i];
    mutex_unlock(&chip->mutex);
    return 0;
}

/**
 * Set the sound levels of every channel
 */
static int xonar_vol_put(struct snd_kcontrol *ctl,
                          struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    unsigned int i;
    int changed;

    changed = 0;
    mutex_lock(&chip->mutex);
    // for each channel
    for (i = 0; i < chip->dac_channels_mixer; ++i)
        // check if new value is different from the old one
        if (value->value.integer.value[i] != chip->dac_volume[i]) {
            // if changed then change volume and set the flag
            chip->dac_volume[i] = value->value.integer.value[i];
            changed = 1;
        }
    // update hardware registers if there were changes to volume levels
    if (changed)
        update_xonar_volume(chip);
    mutex_unlock(&chip->mutex);
    return changed;
}

/**
 * Get information about the state of mute in the sound card.
 */
static int xonar_mute_get(struct snd_kcontrol *ctl,
                        struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;

    mutex_lock(&chip->mutex);
    // get the value
    value->value.integer.value[0] = !chip->dac_mute;
    mutex_unlock(&chip->mutex);
    return 0;
}

/**
 * Set mute status
 */
static int xonar_mute_put(struct snd_kcontrol *ctl,
                        struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    int changed;

    mutex_lock(&chip->mutex);
    // if new value is different than current state
    changed = (!value->value.integer.value[0]) != chip->dac_mute;
    if (changed) {
        // update software flag
        chip->dac_mute = !value->value.integer.value[0];
        // update hardware state
        update_xonar_mute(chip);
    }
    mutex_unlock(&chip->mutex);
    return changed;
}

#define GPIO_D1_FRONT_PANEL	0x0002
/* Entry points for the playback mixer controls */
static struct snd_kcontrol_new xonar_playback_controls[] = {
        {
                .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
                /* Control is of type MIXER */
                .name = "Xonar Volume", /* Name */
                .access = SNDRV_CTL_ELEM_ACCESS_READWRITE,
                .info = xonar_vol_info, /* Volume info */
                .get = xonar_vol_get, /* Get volume */
                .put = xonar_vol_put, /* Set volume */
        },
        // boolean controls don't need custom info function
        {
                .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
                .name = "Xonar Mute Switch",
                .info = snd_ctl_boolean_mono_info,
                .get = xonar_mute_get,
                .put = xonar_mute_put,
        },
        // front panel switch functions are in xonar_hardware.c
        {
                .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
                .name = "Front Panel Playback Switch",
                .info = snd_ctl_boolean_mono_info,
                .get = xonar_gpio_bit_switch_get,
                .put = xonar_gpio_bit_switch_put,
                .private_value = GPIO_D1_FRONT_PANEL,
        }
};

/**
 * It frees every control in one take.
 * Freeing every control isn't optimal, but I don't see any situation where only one control should be freed.
 */
static void oxygen_any_ctl_free(struct snd_kcontrol *ctl) {
    struct xonar *chip = ctl->private_data;
    unsigned int i;

    // comment taken from the kernel :)
    /* I'm too lazy to write a function for each control :-) */
    for (i = 0; i < ARRAY_SIZE(chip->controls); ++i)
        chip->controls[i] = NULL;
}

/**
 * Initliaze controls for the chip
 */
int oxygen_mixer_init(struct xonar *chip) {
    // template will have functions
    struct snd_kcontrol_new template;
    // this will hold control pointer created from template
    struct snd_kcontrol *ctl;
    int i, err;

    for (i = 0; i < ARRAY_SIZE(xonar_playback_controls); ++i) {
        // get the template for current control
        template = xonar_playback_controls[i];
        // create the control struct based on the template
        ctl = snd_ctl_new1(&template, chip);
        if (!ctl)
            return -ENOMEM;
        // attach control struct to the chip
        err = snd_ctl_add(chip->card, ctl);
        if (err < 0)
            return err;

        // save control handler to the chip struct
        chip->controls[i] = ctl;
        // set free function for the control (it frees all controls)
        ctl->private_free = oxygen_any_ctl_free;
    }
}

