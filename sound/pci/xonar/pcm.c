//
// Created by Tomasz Piechocki on 13/12/2020.
//

#include <sound/pcm.h>
#include <linux/module.h>
#include <linux/init.h>
#include <sound/core.h>
#include <linux/pci.h>
#include <sound/pcm_params.h>

#include "main.h"
#include "oxygen_regs.h"


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
    int err;

    runtime->hw = snd_xonar_playback_hw;
    chip->substream = substream;

    /* more hardware-initialization will be done here */
    // TODO CHECK ASSUMPTION: PCM_AC97 is the chosen PCM
    runtime->hw.fifo_size = 0;

    // set step for buffer size changes
    err = snd_pcm_hw_constraint_step(runtime, 0, SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
    if (err < 0)
        return err;

    snd_pcm_set_sync(substream);


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
    struct xonar *chip = snd_pcm_substream_chip(substream);

    oxygen_write32(chip, OXYGEN_DMA_AC97_ADDRESS,
                   (u32)substream->runtime->dma_addr);
    oxygen_write16(chip, OXYGEN_DMA_AC97_ADDRESS + 4,
                   params_buffer_bytes(hw_params) / 4 - 1);
    oxygen_write16(chip, OXYGEN_DMA_AC97_ADDRESS + 6,
                   params_period_bytes(hw_params) / 4 - 1);

    /*snd_pcm_lib_malloc_pages(substream,
                                    params_buffer_bytes(hw_params));*/
    return 0;
}

/* hw_free callback */
static int snd_xonar_pcm_hw_free(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    unsigned int channel = PCM_AC97;
    unsigned int channel_mask = 1 << channel;

    spin_lock_irq(&chip->lock);
    chip->interrupt_mask &= ~channel_mask;
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);

    oxygen_set_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
    oxygen_clear_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
    spin_unlock_irq(&chip->lock);

    /* snd_pcm_lib_free_pages(substream) */
    return 0;
}

/* prepare callback */
static int snd_xonar_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    unsigned int channel = PCM_AC97;
    unsigned int channel_mask = 1 << channel;

    /* set up the hardware with the current configuration
     * for example...
     */
    spin_lock_irq(&chip->lock);
    oxygen_set_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
    oxygen_clear_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);

    if (substream->runtime->no_period_wakeup)
        chip->interrupt_mask &= ~channel_mask;
    else
        chip->interrupt_mask |= channel_mask;
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);
    spin_unlock_irq(&chip->lock);
    return 0;
}

/* trigger callback */
static int snd_xonar_pcm_trigger(struct snd_pcm_substream *substream,
                                 int cmd)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    struct snd_pcm_substream *s;
    unsigned int mask = 0;
    // TODO optionally, used for pause
    int pausing;

    switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_STOP:
            pausing = 0;
            break;
        default:
            return -EINVAL;
    }

    spin_lock(&chip->lock);
    // if not the pause
    if (!pausing) {
        // if start signal
        if (cmd == SNDRV_PCM_TRIGGER_START)
            chip->pcm_running |= mask;
        else    // if stop or suspend signal
            chip->pcm_running &= ~mask;
        oxygen_write8(chip, OXYGEN_DMA_STATUS, chip->pcm_running);
    }
    spin_unlock(&chip->lock);
    return 0;
}

/* pointer callback */
static snd_pcm_uframes_t snd_xonar_pcm_pointer(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;
    unsigned int current_ptr;

    /* get the current hardware pointer */
    current_ptr = xonar_read32(chip, OXYGEN_DMA_AC97_ADDRESS);
    return bytes_to_frames(runtime, current_ptr - (u32)runtime->dma_addr);
}


static struct snd_pcm_ops snd_xonar_playback_ops = {
        .open =         snd_xonar_playback_open,
        .close =        snd_xonar_playback_close,
        .ioctl =        snd_pcm_lib_ioctl,
        .hw_params =    snd_xonar_pcm_hw_params,
        .hw_free =      snd_xonar_pcm_hw_free,
        .prepare =      snd_xonar_pcm_prepare,
        .trigger =      snd_xonar_pcm_trigger,
        .pointer =      snd_xonar_pcm_pointer
};

// TODO redo init
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