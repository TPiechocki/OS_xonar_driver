//
// Created by Tomasz Piechocki on 13/12/2020.
//

#include <sound/pcm.h>
#include <linux/module.h>
#include <linux/init.h>
#include <sound/core.h>
#include <linux/pci.h>

#include "main.h"


// PLAYBACK ONLY

/* hardware definition for playback  */
static struct snd_pcm_hardware snd_xonar_playback_hw = {
        .info = (SNDRV_PCM_INFO_MMAP |
                 SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER |
                 SNDRV_PCM_INFO_MMAP_VALID),
        // TODO optionally pause and resume flags
        // TODO optionally SYNC_START flag
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_48000,
        .rate_min =         48000,
        .rate_max =         48000,
        .channels_min =     2,
        .channels_max =     2,
        .buffer_bytes_max = BUFFER_BYTES_MAX,
        .period_bytes_min = PERIOD_BYTES_MIN,
        .period_bytes_max = BUFFER_BYTES_MAX,
        .periods_min =      1,
        .periods_max =      BUFFER_BYTES_MAX / PERIOD_BYTES_MIN,
};

/* open callback for playback */
static int snd_xonar_playback_open(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;

    runtime->hw = snd_xonar_playback_hw;
    chip->substream = substream;
    /* more hardware-initialization will be done here */
    //....
    return 0;
}

/* close callback for playback */
static int snd_xonar_playback_close(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    chip->substream = NULL;
    /* the hardware-specific codes will be here */
    //....
    return 0;

}
/* hw_params callback */
static int snd_xonar_pcm_hw_params(struct snd_pcm_substream *substream,
                                   struct snd_pcm_hw_params *hw_params)
{
    return snd_pcm_lib_malloc_pages(substream,
                                    params_buffer_bytes(hw_params));
}

/* hw_free callback */
static int snd_xonar_pcm_hw_free(struct snd_pcm_substream *substream)
{
    return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_xonar_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;

    /* set up the hardware with the current configuration
     * for example...
     */
    return 0;
}

/* trigger callback */
static int snd_xonar_pcm_trigger(struct snd_pcm_substream *substream,
                                 int cmd)
{
    switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
            /* do something to start the PCM engine */
            break;
        case SNDRV_PCM_TRIGGER_STOP:
            /* do something to stop the PCM engine */
            break;
        default:
            return -EINVAL;
    }
}

/* pointer callback */
static snd_pcm_uframes_t
snd_xonar_pcm_pointer(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    unsigned int current_ptr;

    /* get the current hardware pointer */
    // TODO
    return current_ptr;
}


static struct snd_pcm_ops snd_xonar_playback_ops = {
        .open =         snd_xonar_playback_open,
        .close =        snd_xonar_playback_close,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_xonar_pcm_hw_params,
        .hw_free =      snd_xonar_pcm_hw_free
};

/* create a single playback stereo pcm device */
static int snd_xonar_new_pcm(struct xonar *chip)
{
    struct snd_pcm *pcm;
    int err;
    // allocate pcm instance; 3 argument is pcm instance id (count from 0), then number of playback devices and
    //      capture devieces
    err = snd_pcm_new(chip->card, "Xonar", 0, 1, 0, &pcm);
    if (err < 0)
        return err;
    // add this device to pcm instance
    pcm->private_data = chip;
    strcpy(pcm->name, "Xonar");
    // add created pcm instance to the device
    chip->pcm = pcm;
    /* set playback operator callbacks */
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
                    &snd_xonar_playback_ops);

    /* pre-allocation of buffers */
    /* NOTE: this may fail */
    snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
                                          snd_dma_pci_data(chip->pci),
                                          DEFAULT_BUFFER_BYTES, BUFFER_BYTES_MAX);
    return 0;
}

