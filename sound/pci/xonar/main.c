// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Asus Xonar DX
 *
 * Copyright (c) Tomasz Piechocki <t.piechocki@yahoo.com>
 *
 * Based on oxygen module made mainly by Clemens Ladisch <clemens@ladisch.de>
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/pci.h>

#include "main.h"
#include "oxygen.h"

/* Module description */
MODULE_AUTHOR("Tomasz Piechocki <t.piechocki@yahoo.com>");
MODULE_DESCRIPTION("Asus Xonar DX driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{ASUS,AV100}}")


/* SNDRV_CARDS: maximum number of cards supported by this module */
/* standard module options for ALSA driver */
static int index[SNDRV_CARDS] = SNDRV_DEFAULT_IDX;
static char *id[SNDRV_CARDS] = SNDRV_DEFAULT_STR;
static bool enable[SNDRV_CARDS] = SNDRV_DEFAULT_ENABLE_PNP;


/* Standard module parameters declaration for this ALSA standard options*/
module_param_array(index, int, NULL, 0444);
MODULE_PARM_DESC(index, "Index value for " CARD_NAME " soundcard.");
module_param_array(id, charp, NULL, 0444);
MODULE_PARM_DESC(id, "ID string for " CARD_NAME " soundcard.");
module_param_array(enable, bool, NULL, 0444);
MODULE_PARM_DESC(enable, "Enable " CARD_NAME " soundcard.");

/* PCI soundcard ID */
static const struct pci_device_id snd_xonar_id[] =  {
        // device is C-Media(vendor) CMI8788(device): ASUS (subvend) Xonar DX (subdevice)
        { PCI_DEVICE_SUB(PCI_VENDOR_ID_CMEDIA, PCI_DEV_ID_CM8788,
                         PCI_VENDOR_ID_ASUS, PCI_DEV_ID_XONARDX) },
        { 0 }       // list terminator
};
/* add IDs table to the module */
MODULE_DEVICE_TABLE(pci, snd_xonar_id);

/** chip-specific destructor
 * (see "PCI Resource Management")
 */
static int snd_xonar_free(struct xonar *chip)
{
    /* disable hardware here if any */
    // TODO /* (not implemented in this document) */

    // release irq
    if (chip->irq >= 0)
        free_irq(chip->irq, chip);
    // release IO region
    pci_release_regions(chip->pci);
    // disable the PCI entry
    pci_disable_device(chip->pci);
    // free the allocated chip data
    kfree(chip);
    return 0;   // success
}

/** component-destructor
 * (see "Management of Cards and Components")
 */
static int snd_xonar_dev_free(struct snd_device *device)
{
    return snd_xonar_free(device->device_data);
}

static irqreturn_t snd_xonar_interrupt(int irq, void *dev_id);

/**
 * Create/initialize chip specific data.
 * @param card - already created card structure
 * @param pci - pci structure for device given from the kernel
 * @param chip -  structure with chip specific data, it will be filled by this function. Data was allocated with
 *                  snd_card_new()
 * @return errors are negative values
 */
static int snd_xonar_create(struct snd_card *card,
        struct pci_dev *pci, struct xonar **rchip) {
    struct xonar *chip;
    int err;
    static struct snd_device_ops ops = {
            .dev_free = snd_xonar_dev_free,
    };

    *rchip = card->private_data;

    /* initialize PCI entry */
    err = pci_enable_device(pci);
    if (err < 0)
        return err;
    /* check PCI availability here aka set DMA ask */
    // I don't see this part in oxygen module so I skip this.

    // enable bus-mastering(?) for the device; it allows the bus to initiate DMA transactions
    pci_set_master(pci);

    chip->card = card;

    /* rest of initialization here; will be implemented
     * later, see "PCI Resource Management"
     */

    // allocate I/O port
    err = pci_request_regions(pci, "Xonar");
    if (err < 0) {
        kfree(chip);
        pci_disable_device(pci);
        return err;
    }
    // TODO optionally add memory length check like in oxygen
    chip->ioport = pci_resource_start(pci, 0);

    // Allocation for interruption source TODO add interrupt handler, check if this funcion works
    // arguments are irq line number, interupt handler, flags (int is shared across PCI devices), module name and
    // data passed to handler, which is chip specific variable here
    if (request_irq(pci->irq, snd_xonar_interrupt,
                    IRQF_SHARED, KBUILD_MODNAME, chip)) {
        printk(KERN_ERR "cannot grab irq %d\n", pci->irq);
        snd_xonar_free(chip);
        return -EBUSY;
    }
    chip->irq = pci->irq;

    // TODO? init pcm_oxygen and mixer_oxygen

    // Register sound device with filled data. Device is the part of the card which perform operations.
    // arguments are: already created card struct, level of the device, pointer to fill the device's data and callbacks
    err = snd_device_new(card, SNDRV_DEV_LOWLEVEL, chip, &ops);
    if (err < 0) {
        snd_xonar_free(chip);
        return err;
    }

    *rchip = chip;
    return 0;
}

/**
 *
 * @param pci
 * @param pci_id
 * @return
 */
static int snd_xonar_probe(struct pci_dev *pci,
                           const struct pci_device_id *pci_id) {
    static int dev;
    int err;

    // Check and increment the device index to find the proper device
    if (dev >= SNDRV_CARDS)
        return -ENODEV;     // no such device
    if (!enable[dev]) {
        ++dev;
        return -ENOENT;     // No such file or directory
    }
    // else this the proper pci card and it is free to use

    // Create the card instance. Card instance manage all components (devices) of the soundcard.
    struct snd_card *card;
    struct xonar *chip;
    // Arguments are: parent PCI device, card index and id, module ptr, size of the extra data and variable ptr to be filled
    err = snd_card_new(&pci->dev, index[dev], id[dev], THIS_MODULE,
                       sizeof(*chip), &card);
    if (err < 0) {
        return err;
    }

    // Create the main component. Look for snd_xonar_create.
    // fill chip variable with chip data, structure from the main.h
    err = snd_xonar_create(card, pci, &chip);
    if (err < 0) {
        // if error then free the card allocated earlier
        snd_card_free(card);
        return err;
    }

    // Set the driver ID and names
    strcpy(card->driver, "Xonar");      // ID string of the chip, must be unique
    strcpy(card->shortname, "Asus Xonar DX");
    // longname is visible in /proc/asound/cards
    sprintf(card->longname, "%s at 0x%lx irq %i",
            card->shortname, chip->ioport, chip->irq);


    // Register the card
    err = snd_card_register(card);
    if (err < 0) {
        // free the allocated card structure
        snd_card_free(card);
        return err;
    }

    // set the pci driver, pointer used in remove callback and ...
    pci_set_drvdata(pci, card);
    // continue probe for other devices
    dev++;
    return 0;
}

/**
 * Destructor is invoked as remove callback
 * Assumption: driver is set in the pci driver data
 * @param pci
 */
static void snd_xonar_remove(struct pci_dev *pci)
{
    // free the card structure
    snd_card_free(pci_get_drvdata(pci));
    // clear the pci driver fot the device
    pci_set_drvdata(pci, NULL);
    // ALSA middle layer will release all the attached components if there were any
}

// prepare the pci driver record with functions
static struct pci_driver driver = {
        .name = KBUILD_MODNAME, // TODO check if works
        .name = "Xonar",
        .id_table = snd_xonar_id,
        .probe = snd_xonar_probe,
        .remove = snd_xonar_remove
};

// module entries
static int __init alsa_card_xonar_init(void)
{
    return pci_register_driver(&driver);
}

static void __exit alsa_card_xonar_exit(void)
{
    pci_unregister_driver(&driver);
}

module_init(alsa_card_xonar_init)
module_exit(alsa_card_xonar_exit)

// PCM RELATED FRAGMENT

#include <sound/pcm.h>
#include <sound/pcm_params.h>

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


static irqreturn_t snd_xonar_interrupt(int irq, void *dev_id)
{
    struct xonar *chip = dev_id;
    spin_lock(&chip->lock);

    unsigned int status = xonar_read16(chip, OXYGEN_INTERRUPT_STATUS);

    if (!status)
        return IRQ_NONE;

    /* call updater, unlock before it */
    spin_unlock(&chip->lock);
    snd_pcm_period_elapsed(chip->substream);
    spin_lock(&chip->lock);
    /* acknowledge the interrupt if necessary */

    spin_unlock(&chip->lock);
    return IRQ_HANDLED;
}
