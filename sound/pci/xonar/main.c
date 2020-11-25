// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Asus Xonar DX
 *
 * Copyright (c) Tomasz Piechocki <t.piechocki@yahoo.com>
 *
 * Based on oxygen module made mainly by Clemens Ladisch <clemens@ladisch.de>
 */


#include <linux/module.h>
#include <sound/core.h>
#include <sound/initval.h>
#include <sound/pcm.h>
#include <linux/pci.h>

#include "main.h"

/* Module description */
MODULE_AUTHOR("Tomasz Piechocki <t.piechocki@yahoo.com>");
MODULE_DESCRIPTION("Asus Xonar DX driver");
MODULE_LICENSE("GPL v2");
MODULE_SUPPORTED_DEVICE("{{ASUS,AV100}}");


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
                         PCI_VENDOR_ID_ASUS, PCI_DEV_ID_XONARDX) }
};
/* add IDs table to the module */
MODULE_DEVICE_TABLE(pci, snd_xonar_id);

/** chip-specific destructor
 * (see "PCI Resource Management")
 */
static int snd_xonar_free(struct xonar *chip)
{
    /* will be implemented later... */
}

/** component-destructor
 * (see "Management of Cards and Components")
 */
static int snd_xonar_dev_free(struct snd_device *device)
{
    return snd_xonar_free(device->device_data);
}


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

    /* check PCI availability here
         * (see "PCI Resource Management")
         */
    // ....

    chip->card = card;

    /* rest of initialization here; will be implemented
     * later, see "PCI Resource Management"
     */
    // ....

    // Register PCM sound device with filled data. Device is the part of the card which perform operations.
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

