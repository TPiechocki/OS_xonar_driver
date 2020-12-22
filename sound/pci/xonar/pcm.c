//
// Created by Tomasz Piechocki on 13/12/2020.
//

#include <linux/pci.h>
#include <sound/control.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>

#include "main.h"
#include "oxygen_regs.h"


// buffer limit sizes for pcm stream
#define PERIOD_BYTES_MIN		64
/* most DMA channels have a 16-bit counter for 32-bit words */
#define BUFFER_BYTES_MAX		((1 << 16) * 4)
/* the multichannel DMA channel has a 24-bit counter */
#define BUFFER_BYTES_MAX_MULTICH	((1 << 24) * 4)

#define DEFAULT_BUFFER_BYTES		(BUFFER_BYTES_MAX / 2)
#define DEFAULT_BUFFER_BYTES_MULTICH	(1024 * 1024)

#define FIFO_BYTES			256
#define FIFO_BYTES_MULTICH		1024


// PLAYBACK ONLY

/* hardware definition for playback  */
static struct snd_pcm_hardware snd_xonar_playback_hw = {
        .info = (SNDRV_PCM_INFO_MMAP |
                 SNDRV_PCM_INFO_INTERLEAVED |
                 SNDRV_PCM_INFO_BLOCK_TRANSFER |
                 SNDRV_PCM_INFO_MMAP_VALID |
                 SNDRV_PCM_INFO_PAUSE |
                 SNDRV_PCM_INFO_NO_PERIOD_WAKEUP),
        .formats =          SNDRV_PCM_FMTBIT_S16_LE,
        .rates =            SNDRV_PCM_RATE_48000,
        .rate_min =         48000,
        .rate_max =         48000,
        .channels_min =     2,
        .channels_max =     8,
        .buffer_bytes_max = BUFFER_BYTES_MAX_MULTICH,
        .period_bytes_min = PERIOD_BYTES_MIN,
        .period_bytes_max = BUFFER_BYTES_MAX_MULTICH,
        .periods_min =      1,
        .periods_max =      BUFFER_BYTES_MAX_MULTICH / PERIOD_BYTES_MIN,
        .fifo_size =        FIFO_BYTES_MULTICH
};

/* open callback for playback */
static int snd_xonar_playback_open(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    struct snd_pcm_runtime *runtime = substream->runtime;
    int err;

    runtime->private_data = (void *)(uintptr_t)PCM_MULTICH;

    runtime->hw = snd_xonar_playback_hw;
    chip->substream = substream;

    // PCM_MULTICH is the chosen PCM
    runtime->hw.channels_max = 8;

    // set step for buffer size changes
    err = snd_pcm_hw_constraint_step(runtime, 0,
                                     SNDRV_PCM_HW_PARAM_PERIOD_BYTES, 32);
    if (err < 0)
        return err;
    err = snd_pcm_hw_constraint_step(runtime, 0,
                                     SNDRV_PCM_HW_PARAM_BUFFER_BYTES, 32);
    if (err < 0)
        return err;

    // group channels in pairs
    err = snd_pcm_hw_constraint_step(runtime, 0,
                                     SNDRV_PCM_HW_PARAM_CHANNELS,
                                     2);
    if (err < 0)
        return err;

    snd_pcm_set_sync(substream);
    chip->substream = substream;

    mutex_lock(&chip->mutex);
    chip->pcm_active |= 1 << PCM_MULTICH;
    mutex_unlock(&chip->mutex);

    return 0;
}

/* close callback for playback */
static int snd_xonar_playback_close(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    chip->substream = NULL;

    chip->pcm_active &= ~(1 << PCM_MULTICH);

    return 0;

}
/* hw_params callback */
static int snd_xonar_pcm_hw_params(struct snd_pcm_substream *substream,
                                   struct snd_pcm_hw_params *hw_params)
{
    int retcode = snd_pcm_lib_malloc_pages(substream,
                                           params_buffer_bytes(hw_params));
    struct xonar *chip = snd_pcm_substream_chip(substream);

    // activate DMA memory for the stream
    oxygen_write32(chip, OXYGEN_DMA_MULTICH_ADDRESS,
                   (u32)substream->runtime->dma_addr);
    oxygen_write16(chip, OXYGEN_DMA_MULTICH_COUNT,
                   params_buffer_bytes(hw_params) / 4 - 1);
    oxygen_write16(chip, OXYGEN_DMA_MULTICH_TCOUNT,
                   params_period_bytes(hw_params) / 4 - 1);

    // MULTICH
    mutex_lock(&chip->mutex);
    spin_lock_irq(&chip->lock);
    // set play channels at 4
    oxygen_write8_masked(chip, OXYGEN_PLAY_CHANNELS,
                         OXYGEN_PLAY_CHANNELS_2,
                         OXYGEN_PLAY_CHANNELS_MASK);
    // proper byts format for play (16 bits)
    oxygen_write8_masked(chip, OXYGEN_PLAY_FORMAT,
                         OXYGEN_FORMAT_16 << OXYGEN_MULTICH_FORMAT_SHIFT,
                         OXYGEN_MULTICH_FORMAT_MASK);
    // set stream details through I2S like stream Hz, left justifies, 16 bits
    oxygen_write16_masked(chip, OXYGEN_I2S_MULTICH_FORMAT,
                          OXYGEN_RATE_48000 |
                          OXYGEN_I2S_FORMAT_LJUST |
                          OXYGEN_I2S_MCLK(OXYGEN_MCLKS(256, 128, 128)) |
                          OXYGEN_I2S_BITS_16,
                          OXYGEN_I2S_RATE_MASK |
                          OXYGEN_I2S_FORMAT_MASK |
                          OXYGEN_I2S_MCLK_MASK |
                          OXYGEN_I2S_BITS_MASK);
    // disable spdif
    oxygen_write32(chip, OXYGEN_SPDIF_CONTROL,
                   xonar_read32(chip, OXYGEN_SPDIF_CONTROL) & ~OXYGEN_SPDIF_OUT_ENABLE);


    spin_unlock_irq(&chip->lock);

    // set dacs hardware parameters
    set_cs43xx_params(chip, hw_params);

    // DAC routing means that different channels will go to different outputs of the card
    unsigned int reg_value = (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
                            (1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
                            (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
                            (3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT);

    oxygen_write16_masked(chip, OXYGEN_PLAY_ROUTING, reg_value,
                          OXYGEN_PLAY_DAC0_SOURCE_MASK |
                          OXYGEN_PLAY_DAC1_SOURCE_MASK |
                          OXYGEN_PLAY_DAC2_SOURCE_MASK |
                          OXYGEN_PLAY_DAC3_SOURCE_MASK);


    mutex_unlock(&chip->mutex);


    return retcode;
}

/* hw_free callback */
static int snd_xonar_pcm_hw_free(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    unsigned int channel = PCM_MULTICH;
    unsigned int channel_mask = 1 << channel;

    spin_lock_irq(&chip->lock);
    chip->interrupt_mask &= ~channel_mask;
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, chip->interrupt_mask);

    oxygen_set_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
    oxygen_clear_bits8(chip, OXYGEN_DMA_FLUSH, channel_mask);
    spin_unlock_irq(&chip->lock);

    return snd_pcm_lib_free_pages(substream);
}

/* prepare callback */
static int snd_xonar_pcm_prepare(struct snd_pcm_substream *substream)
{
    struct xonar *chip = snd_pcm_substream_chip(substream);
    unsigned int channel = PCM_MULTICH;
    unsigned int channel_mask = 1 << channel;

    spin_lock_irq(&chip->lock);
    // clear DMA memory
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
    int pausing;

    // set if this was pause or not
    switch (cmd) {
        case SNDRV_PCM_TRIGGER_START:
        case SNDRV_PCM_TRIGGER_STOP:
        case SNDRV_PCM_TRIGGER_SUSPEND:
            pausing = 0;
            break;
        case SNDRV_PCM_TRIGGER_PAUSE_PUSH:
        case SNDRV_PCM_TRIGGER_PAUSE_RELEASE:
            pausing = 1;
            break;
        default:
            return -EINVAL;
    }

    // only one entry for this chip in this situation
    snd_pcm_group_for_each_entry(s, substream) {
        if (snd_pcm_substream_chip(s) == chip) {
            // add substream to mask, this is trigger action
            mask |= 1 << (unsigned int)(uintptr_t)substream->runtime->private_data;
            // mark this substream as handled
            snd_pcm_trigger_done(s, substream);
        }
    }

    spin_lock(&chip->lock);
    // if not the pause
    if (!pausing) {
        // if start signal
        if (cmd == SNDRV_PCM_TRIGGER_START)
            chip->pcm_running |= mask;
        else    // if stop or suspend signal
            chip->pcm_running &= ~mask;
        printk(KERN_ERR "Trigger DMA Status write: %d", chip->pcm_running);
        // set DMA status to closed or open stream
        oxygen_write8(chip, OXYGEN_DMA_STATUS, chip->pcm_running);
    } else {        // if pause
        if (cmd == SNDRV_PCM_TRIGGER_PAUSE_PUSH)
            oxygen_set_bits8(chip, OXYGEN_DMA_PAUSE, mask);
        else
            oxygen_clear_bits8(chip, OXYGEN_DMA_PAUSE, mask);
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
    current_ptr = xonar_read32(chip, OXYGEN_DMA_MULTICH_ADDRESS);
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


/* create a single playback 4-channel pcm device */
int snd_xonar_new_pcm(struct xonar *chip)
{
    struct snd_pcm *pcm;
    int err;
    // allocate pcm instance; 3 argument is pcm instance id (count from 0), then number of playback devices and
    //      capture devices
    err = snd_pcm_new(chip->card, "Xonar", 0, 1, 0, &pcm);
    if (err < 0)
        return err;
    /* set playback operator callbacks */
    snd_pcm_set_ops(pcm, SNDRV_PCM_STREAM_PLAYBACK,
                    &snd_xonar_playback_ops);
    // add this device to pcm instance
    pcm->private_data = chip;
    strcpy(pcm->name, "Xonar");
    // add created pcm instance to the device
    chip->pcm = pcm;

    /* pre-allocation of buffers */
    /* NOTE: this may fail */
    snd_pcm_lib_preallocate_pages_for_all(pcm, SNDRV_DMA_TYPE_DEV,
                                          snd_dma_pci_data(chip->pci),
                                          DEFAULT_BUFFER_BYTES_MULTICH, BUFFER_BYTES_MAX_MULTICH);

    return 0;
}
