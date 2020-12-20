//
// Created by Tomasz Piechocki on 19/12/2020.
//

#include <linux/mutex.h>
#include <sound/control.h>
#include <sound/core.h>

#include "main.h"

/**
 * Get volume set in the dac
 */
static int xonar_vol_info(struct snd_kcontrol *ctl,
                           struct snd_ctl_elem_info *info)
{
    struct xonar *chip = ctl->private_data;

    info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
    info->count = chip->dac_channels_mixer;
    info->value.integer.min = chip->dac_volume_min;
    info->value.integer.max = chip->dac_volume_max;
    return 0;
}

static int xonar_vol_get(struct snd_kcontrol *ctl,
                          struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    unsigned int i;

    mutex_lock(&chip->mutex);
    for (i = 0; i < chip->dac_channels_mixer; ++i)
        value->value.integer.value[i] = chip->dac_volume[i];
    mutex_unlock(&chip->mutex);
    return 0;
}

static int xonar_vol_put(struct snd_kcontrol *ctl,
                          struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    unsigned int i;
    int changed;

    changed = 0;
    mutex_lock(&chip->mutex);
    for (i = 0; i < chip->dac_channels_mixer; ++i)
        if (value->value.integer.value[i] != chip->dac_volume[i]) {
            chip->dac_volume[i] = value->value.integer.value[i];
            changed = 1;
        }
    if (changed)
        update_xonar_volume(chip);
    mutex_unlock(&chip->mutex);
    return changed;
}

static int xonar_mute_get(struct snd_kcontrol *ctl,
                        struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;

    mutex_lock(&chip->mutex);
    value->value.integer.value[0] = !chip->dac_mute;
    mutex_unlock(&chip->mutex);
    return 0;
}

static int xonar_mute_put(struct snd_kcontrol *ctl,
                        struct snd_ctl_elem_value *value)
{
    struct xonar *chip = ctl->private_data;
    int changed;

    mutex_lock(&chip->mutex);
    changed = (!value->value.integer.value[0]) != chip->dac_mute;
    if (changed) {
        chip->dac_mute = !value->value.integer.value[0];
        update_xonar_mute(chip);
    }
    mutex_unlock(&chip->mutex);
    return changed;
}

/* Entry points for the playback mixer */
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
        {
                .iface = SNDRV_CTL_ELEM_IFACE_MIXER,
                .name = "Xonar Mute Switch",
                .info = snd_ctl_boolean_mono_info,
                .get = xonar_mute_get,
                .put = xonar_mute_put,
        }
};

static void oxygen_any_ctl_free(struct snd_kcontrol *ctl) {
    struct xonar *chip = ctl->private_data;
    unsigned int i;

    /* I'm too lazy to write a function for each control :-) */
    for (i = 0; i < ARRAY_SIZE(chip->controls); ++i)
        chip->controls[i] = NULL;
}

int oxygen_mixer_init(struct xonar *chip) {
    struct snd_kcontrol_new template;
    struct snd_kcontrol *ctl;
    int i, err;

    for (i = 0; i < ARRAY_SIZE(xonar_playback_controls); ++i) {
        template = xonar_playback_controls[i];

        ctl = snd_ctl_new1(&template, chip);
        if (!ctl)
            return -ENOMEM;
        err = snd_ctl_add(chip->card, ctl);
        if (err < 0)
            return err;

        chip->controls[i] = ctl;
        ctl->private_free = oxygen_any_ctl_free;
    }
}

