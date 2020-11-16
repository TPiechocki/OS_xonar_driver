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

