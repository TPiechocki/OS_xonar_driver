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
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#include <sound/core.h>
#include <sound/initval.h>
#include <linux/pci.h>
#include <sound/pcm.h>

#include <sound/ac97_codec.h>
#include <sound/asoundef.h>
#include <sound/core.h>
#include <sound/info.h>
#include <sound/mpu401.h>


#include "main.h"
#include "oxygen_regs.h"

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

static void snd_xonar_shutdown(struct pci_dev *pci);
/**
 * Chip-specific destructor
 */
static void snd_xonar_free(struct snd_card *card)
{
    struct xonar *chip = card->private_data;

    // same actions as for shutdown without hardware cleanup
    spin_lock_irq(&chip->lock);
    chip->interrupt_mask = 0;
    chip->pcm_running = 0;
    oxygen_write16(chip, OXYGEN_DMA_STATUS, 0);
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
    spin_unlock_irq(&chip->lock);

    // release irq
    if (chip->irq >= 0)
        free_irq(chip->irq, chip);
    flush_work(&chip->gpio_work);
    // destroy mutex
    mutex_destroy(&chip->mutex);
    // release IO region
    pci_release_regions(chip->pci);
    // disable the PCI entry
    pci_disable_device(chip->pci);
}

/*
 * When external power changed than perform the work on the chip
 */
static void xonar_gpio_changed(struct work_struct *work)
{
    struct xonar *chip = container_of(work, struct xonar, gpio_work);

    xonar_ext_power_gpio_changed(chip);
}

/**
 * Interrupt handler
 * @param irq - irq number
 * @param dev_id - chip pointer
 * @return
 */
static irqreturn_t snd_xonar_interrupt(int irq, void *dev_id)
{
    struct xonar *chip = dev_id;

    // read the information whether this chip was interrupted
    unsigned int status = xonar_read16(chip, OXYGEN_INTERRUPT_STATUS);
    // if interrupt doesn't relate to this chip than skip handling
    if (!status)
        return IRQ_NONE;

    // interrupt handler is atomic so use the spin lock
    spin_lock(&chip->lock);

    // get only bits which contain proper information
    unsigned int clear = status & (OXYGEN_CHANNEL_A |
                      OXYGEN_CHANNEL_B |
                      OXYGEN_CHANNEL_C |
                      OXYGEN_CHANNEL_SPDIF |
                      OXYGEN_CHANNEL_MULTICH |
                      OXYGEN_CHANNEL_AC97 |
                      OXYGEN_INT_SPDIF_IN_DETECT |
                      OXYGEN_INT_GPIO |
                      OXYGEN_INT_AC97);
    if (clear) {
        // spdif is not used
        if (clear & OXYGEN_INT_SPDIF_IN_DETECT)
            chip->interrupt_mask &= ~OXYGEN_INT_SPDIF_IN_DETECT;
        oxygen_write16(chip, OXYGEN_INTERRUPT_MASK,
                       chip->interrupt_mask & ~clear);
        oxygen_write16(chip, OXYGEN_INTERRUPT_MASK,
                       chip->interrupt_mask);
    }

    /* call updater, unlock before it */
    spin_unlock(&chip->lock);

    // most common interrupt is dma buffer end
    // check if it is the case
    unsigned int elapsed_streams = status & chip->pcm_running;
    if (elapsed_streams && chip->substream)
        // if yes then make cycle in DMA buffer
        snd_pcm_period_elapsed(chip->substream);
    spin_lock(&chip->lock);

    // perform tasks if needed
    if (status & OXYGEN_INT_GPIO)
        schedule_work(&chip->gpio_work);

    if (status & OXYGEN_INT_AC97)
        wake_up(&chip->ac97_waitqueue);

    spin_unlock(&chip->lock);
    return IRQ_HANDLED;
}


static void configure_pcie_bridge(struct pci_dev *pci);
static void xonar_proc_read(struct snd_info_entry *entry, struct snd_info_buffer *buffer);
/**
 * Create/initialize chip specific data.
 * @param card - already created card structure
 * @param pci - pci structure for device given from the kernel
 * @param chip -  structure with chip specific data, it will be filled by this function. Data was allocated with
 *                  snd_card_new()
 * @return errors are negative values
 */
static int snd_xonar_create(struct snd_card *card,
        struct pci_dev *pci) {
    struct xonar *chip;
    int err;

    chip = card->private_data;

    /* initialize PCI entry */
    err = pci_enable_device(pci);
    if (err < 0) {
        snd_card_free(card);
        return err;
    }
    /* check PCI availability here aka set DMA ask */
    // I don't see this part in oxygen module so I skip this.

    // allocate I/O port
    err = pci_request_regions(pci, "Xonar");
    if (err < 0) {
        dev_err(card->dev, "cannot reserve PCI resources\n");
        pci_disable_device(pci);
        snd_card_free(card);
        return err;
    }
    // check if the length of the PCI area has enough size
    if (!(pci_resource_flags(pci, 0) & IORESOURCE_IO) ||
        pci_resource_len(pci, 0) < OXYGEN_IO_SIZE) {
        dev_err(card->dev, "invalid PCI I/O range\n");
        err = -ENXIO;
        pci_release_regions(pci);
        pci_disable_device(pci);
        snd_card_free(card);
        return err;
    }
    chip->ioport = pci_resource_start(pci, 0);


    // enable bus-mastering(?) for the device; it allows the bus to initiate DMA transactions
    pci_set_master(pci);
    card->private_free = snd_xonar_free;

    // idk what it does, but it gets called
    configure_pcie_bridge(pci);

    // init oxygen hardware
    oxygen_init(chip);
    // init xonar DACs
    xonar_dx_init(chip);


    // Allocation for interruption source
    // arguments are irq line number, interrupt handler, flags (int is shared across PCI devices), module name and
    // data passed to handler, which is chip specific variable here
    if (request_irq(pci->irq, snd_xonar_interrupt,
                    IRQF_SHARED, KBUILD_MODNAME, chip)) {
        printk(KERN_ERR "cannot grab irq %d\n", pci->irq);
        snd_card_free(card);
        return -EBUSY;
    }
    chip->irq = pci->irq;

    // init pcm stream
    err = snd_xonar_new_pcm(chip);
    if (err < 0) {
        snd_card_free(card);
        return err;
    }

    // init mixer controls
    err = oxygen_mixer_init(chip);
    if (err < 0) {
        snd_card_free(card);
        return err;
    }

    // PROC file with registers dump
    snd_card_ro_proc_new(chip->card, "xonar", chip, xonar_proc_read);

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
    // attach structures to the chip structure
    chip = card->private_data;
    chip->card = card;
    chip->pci = pci;

    // init spinlock and mutex
    spin_lock_init(&chip->lock);
    mutex_init(&chip->mutex);
    // initialize ac97 queue which is used on writes to ac97 device, not used as it is input device
    INIT_WORK(&chip->gpio_work, xonar_gpio_changed);
    init_waitqueue_head(&chip->ac97_waitqueue);


    // Create the main component. Look for snd_xonar_create.
    // Fill chip variable with chip data, structure from the main.h
    // Set the hardware, set the interrupts etc.
    err = snd_xonar_create(card, pci);
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


    // Register the card in the ALSA
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
    // ALSA middle layer will release all the attached components if there were any
}

static void snd_xonar_shutdown(struct pci_dev *pci) {
    struct snd_card *card = pci_get_drvdata(pci);
    struct xonar *chip = card->private_data;

    spin_lock_irq(&chip->lock);
    // disable pcm and turn off interupts
    chip->interrupt_mask = 0;
    chip->pcm_running = 0;
    // disable pcm DMA
    oxygen_write16(chip, OXYGEN_DMA_STATUS, 0);
    // disable interrupts in chip
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
    spin_unlock_irq(&chip->lock);

    // chip specific cleanup
    printk(KERN_ERR "PERFORM XONAR SHUTDOWN");
    // clean up xonar specific data
    xonar_dx_cleanup(chip);
}

// prepare the pci driver record with functions
static struct pci_driver driver = {
        .name = KBUILD_MODNAME,
        .id_table = snd_xonar_id,
        .probe = snd_xonar_probe,
        .remove = snd_xonar_remove,
        .shutdown = snd_xonar_shutdown
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


// PROC
/**
 * Read only proc entry
 */
static void xonar_proc_read(struct snd_info_entry *entry,
                             struct snd_info_buffer *buffer)
{
    struct xonar *chip = entry->private_data;
    int i, j;

    switch (xonar_read8(chip, OXYGEN_REVISION) & OXYGEN_PACKAGE_ID_MASK) {
        case OXYGEN_PACKAGE_ID_8786: i = '6'; break;
        case OXYGEN_PACKAGE_ID_8787: i = '7'; break;
        case OXYGEN_PACKAGE_ID_8788: i = '8'; break;
        default:                     i = '?'; break;
    }
    // print processor info
    snd_iprintf(buffer, "CMI878%c:\n", i);
    for (i = 0; i < OXYGEN_IO_SIZE; i += 0x10) {
        snd_iprintf(buffer, "%02x:", i);
        for (j = 0; j < 0x10; ++j)
            snd_iprintf(buffer, " %02x", xonar_read8(chip, i + j));
        snd_iprintf(buffer, "\n");
    }
    if (mutex_lock_interruptible(&chip->mutex) < 0)
        return;
    if (chip->has_ac97_1) {
        snd_iprintf(buffer, "\nAC97 2:\n");
        for (i = 0; i < 0x80; i += 0x10) {
            snd_iprintf(buffer, "%02x:", i);
            for (j = 0; j < 0x10; j += 2)
                snd_iprintf(buffer, " %04x",
                            oxygen_read_ac97(chip, 1, i + j));
            snd_iprintf(buffer, "\n");
        }
    }
    mutex_unlock(&chip->mutex);
    // dump hardware registers of the DACs
    dump_registers(chip, buffer);
}

// OXYGEN
// it's scary to move this fuction, but it's hardware configuration of card processor
// I removed some unneeded parts and added some comments here.
static void oxygen_init(struct xonar *chip)
{
    unsigned int i;

    chip->dac_routing = 1;
    for (i = 0; i < 8; ++i)
        chip->dac_volume[i] = 127;      // max volume
    chip->dac_mute = 0;         // 0 means not muted
    chip->spdif_playback_enable = 0;
    chip->spdif_bits = OXYGEN_SPDIF_C | OXYGEN_SPDIF_ORIGINAL |
                       (IEC958_AES1_CON_PCM_CODER << OXYGEN_SPDIF_CATEGORY_SHIFT);
    chip->spdif_pcm_bits = chip->spdif_bits;

    if (!(xonar_read8(chip, OXYGEN_REVISION) & OXYGEN_REVISION_2))
        oxygen_set_bits8(chip, OXYGEN_MISC,
                         OXYGEN_MISC_PCI_MEM_W_1_CLOCK);

    i = xonar_read16(chip, OXYGEN_AC97_CONTROL);
    chip->has_ac97_0 = (i & OXYGEN_AC97_CODEC_0) != 0;
    chip->has_ac97_1 = (i & OXYGEN_AC97_CODEC_1) != 0;

    oxygen_write8_masked(chip, OXYGEN_FUNCTION,
                         OXYGEN_FUNCTION_RESET_CODEC |
                         OXYGEN_FUNCTION_2WIRE,
                         OXYGEN_FUNCTION_RESET_CODEC |
                         OXYGEN_FUNCTION_2WIRE_SPI_MASK |
                         OXYGEN_FUNCTION_ENABLE_SPI_4_5);
    oxygen_write8(chip, OXYGEN_DMA_STATUS, 0);
    oxygen_write8(chip, OXYGEN_DMA_PAUSE, 0);
    oxygen_write8(chip, OXYGEN_PLAY_CHANNELS,
                  OXYGEN_PLAY_CHANNELS_4 |
                  OXYGEN_DMA_A_BURST_8 |
                  OXYGEN_DMA_MULTICH_BURST_8);
    oxygen_write16(chip, OXYGEN_INTERRUPT_MASK, 0);
    oxygen_write8_masked(chip, OXYGEN_MISC,
                         0,
                         OXYGEN_MISC_WRITE_PCI_SUBID |
                         OXYGEN_MISC_REC_C_FROM_SPDIF |
                         OXYGEN_MISC_REC_B_FROM_AC97 |
                         OXYGEN_MISC_REC_A_FROM_MULTICH |
                         OXYGEN_MISC_MIDI);
    oxygen_write8(chip, OXYGEN_REC_FORMAT,
                  (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_A_SHIFT) |
                  (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_B_SHIFT) |
                  (OXYGEN_FORMAT_16 << OXYGEN_REC_FORMAT_C_SHIFT));
    oxygen_write8(chip, OXYGEN_PLAY_FORMAT,
                  (OXYGEN_FORMAT_16 << OXYGEN_SPDIF_FORMAT_SHIFT) |
                  (OXYGEN_FORMAT_16 << OXYGEN_MULTICH_FORMAT_SHIFT));
    oxygen_write8(chip, OXYGEN_REC_CHANNELS, OXYGEN_REC_CHANNELS_2_2_2);
    oxygen_write16(chip, OXYGEN_I2S_MULTICH_FORMAT,
                   OXYGEN_RATE_44100 |
                   chip->dac_i2s_format |
                   OXYGEN_I2S_MCLK(chip->dac_mclks) |
                   OXYGEN_I2S_BITS_16 |
                   OXYGEN_I2S_MASTER |
                   OXYGEN_I2S_BCLK_64);

    // not used
    oxygen_write16(chip, OXYGEN_I2S_A_FORMAT,
                       OXYGEN_I2S_MASTER |
                       OXYGEN_I2S_MUTE_MCLK);
    // not used
    oxygen_write16(chip, OXYGEN_I2S_B_FORMAT,
                       OXYGEN_I2S_MASTER |
                       OXYGEN_I2S_MUTE_MCLK);
    // not used
    oxygen_write16(chip, OXYGEN_I2S_C_FORMAT,
                       OXYGEN_I2S_MASTER |
                       OXYGEN_I2S_MUTE_MCLK);
    // disable spdif_out
    oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
                        OXYGEN_SPDIF_OUT_ENABLE |
                        OXYGEN_SPDIF_LOOPBACK);
    // not used
    oxygen_clear_bits32(chip, OXYGEN_SPDIF_CONTROL,
                            OXYGEN_SPDIF_SENSE_MASK |
                            OXYGEN_SPDIF_LOCK_MASK |
                            OXYGEN_SPDIF_RATE_MASK);
    oxygen_write32(chip, OXYGEN_SPDIF_OUTPUT_BITS, chip->spdif_bits);
    oxygen_write16(chip, OXYGEN_2WIRE_BUS_STATUS,
                   OXYGEN_2WIRE_LENGTH_8 |
                   OXYGEN_2WIRE_INTERRUPT_MASK |
                   OXYGEN_2WIRE_SPEED_STANDARD);
    // not used
    oxygen_clear_bits8(chip, OXYGEN_MPU401_CONTROL, OXYGEN_MPU401_LOOPBACK);
    oxygen_write8(chip, OXYGEN_GPI_INTERRUPT_MASK, 0);
    oxygen_write16(chip, OXYGEN_GPIO_INTERRUPT_MASK, 0);
    // set routing through good outputs
    oxygen_write16(chip, OXYGEN_PLAY_ROUTING,
                   OXYGEN_PLAY_MULTICH_I2S_DAC |
                   OXYGEN_PLAY_SPDIF_SPDIF |
                   (0 << OXYGEN_PLAY_DAC0_SOURCE_SHIFT) |
                   (1 << OXYGEN_PLAY_DAC1_SOURCE_SHIFT) |
                   (2 << OXYGEN_PLAY_DAC2_SOURCE_SHIFT) |
                   (3 << OXYGEN_PLAY_DAC3_SOURCE_SHIFT));
    oxygen_write8(chip, OXYGEN_REC_ROUTING,
                  OXYGEN_REC_A_ROUTE_I2S_ADC_1 |
                  OXYGEN_REC_B_ROUTE_I2S_ADC_2 |
                  OXYGEN_REC_C_ROUTE_SPDIF);
    oxygen_write8(chip, OXYGEN_ADC_MONITOR, 0);
    oxygen_write8(chip, OXYGEN_A_MONITOR_ROUTING,
                  (0 << OXYGEN_A_MONITOR_ROUTE_0_SHIFT) |
                  (1 << OXYGEN_A_MONITOR_ROUTE_1_SHIFT) |
                  (2 << OXYGEN_A_MONITOR_ROUTE_2_SHIFT) |
                  (3 << OXYGEN_A_MONITOR_ROUTE_3_SHIFT));

    if (chip->has_ac97_0 | chip->has_ac97_1)
        oxygen_write8(chip, OXYGEN_AC97_INTERRUPT_MASK,
                      OXYGEN_AC97_INT_READ_DONE |
                      OXYGEN_AC97_INT_WRITE_DONE);
    else
        oxygen_write8(chip, OXYGEN_AC97_INTERRUPT_MASK, 0);
    oxygen_write32(chip, OXYGEN_AC97_OUT_CONFIG, 0);
    oxygen_write32(chip, OXYGEN_AC97_IN_CONFIG, 0);

    // chip has_ac97_0, but not used because it's used only for input
    if (!(chip->has_ac97_0 | chip->has_ac97_1))
        oxygen_set_bits16(chip, OXYGEN_AC97_CONTROL,
                          OXYGEN_AC97_CLOCK_DISABLE);
    if (!chip->has_ac97_0) {
        oxygen_set_bits16(chip, OXYGEN_AC97_CONTROL,
                          OXYGEN_AC97_NO_CODEC_0);
    } else {
        oxygen_write_ac97(chip, 0, AC97_RESET, 0);
        msleep(1);
        oxygen_ac97_set_bits(chip, 0, CM9780_GPIO_SETUP,
                             CM9780_GPIO0IO | CM9780_GPIO1IO);
        oxygen_ac97_set_bits(chip, 0, CM9780_MIXER,
                             CM9780_BSTSEL | CM9780_STRO_MIC |
                             CM9780_MIX2FR | CM9780_PCBSW);
        oxygen_ac97_set_bits(chip, 0, CM9780_JACK,
                             CM9780_RSOE | CM9780_CBOE |
                             CM9780_SSOE | CM9780_FROE |
                             CM9780_MIC2MIC | CM9780_LI2LI);
        oxygen_write_ac97(chip, 0, AC97_MASTER, 0x0000);
        oxygen_write_ac97(chip, 0, AC97_PC_BEEP, 0x8000);
        oxygen_write_ac97(chip, 0, AC97_MIC, 0x8808);
        oxygen_write_ac97(chip, 0, AC97_LINE, 0x0808);
        oxygen_write_ac97(chip, 0, AC97_CD, 0x8808);
        oxygen_write_ac97(chip, 0, AC97_VIDEO, 0x8808);
        oxygen_write_ac97(chip, 0, AC97_AUX, 0x8808);
        oxygen_write_ac97(chip, 0, AC97_REC_GAIN, 0x8000);
        oxygen_write_ac97(chip, 0, AC97_CENTER_LFE_MASTER, 0x8080);
        oxygen_write_ac97(chip, 0, AC97_SURROUND_MASTER, 0x8080);
        oxygen_ac97_clear_bits(chip, 0, CM9780_GPIO_STATUS,
                               CM9780_GPO0);
        /* power down unused ADCs and DACs */
        oxygen_ac97_set_bits(chip, 0, AC97_POWERDOWN,
                             AC97_PD_PR0 | AC97_PD_PR1);
        oxygen_ac97_set_bits(chip, 0, AC97_EXTENDED_STATUS,
                             AC97_EA_PRI | AC97_EA_PRJ | AC97_EA_PRK);
    }
}

static void configure_pcie_bridge(struct pci_dev *pci)
{
    enum { PEX811X, PI7C9X110, XIO2001 };
    static const struct pci_device_id bridge_ids[] = {
            { PCI_VDEVICE(PLX, 0x8111), .driver_data = PEX811X },
            { PCI_VDEVICE(PLX, 0x8112), .driver_data = PEX811X },
            { PCI_DEVICE(0x12d8, 0xe110), .driver_data = PI7C9X110 },
            { PCI_VDEVICE(TI, 0x8240), .driver_data = XIO2001 },
            { }
    };
    struct pci_dev *bridge;
    const struct pci_device_id *p_id;
    u32 tmp;

    if (!pci->bus || !pci->bus->self)
        return;
    bridge = pci->bus->self;

    p_id = pci_match_id(bridge_ids, bridge);
    if (!p_id)
        return;

    // it is called in the driver
    printk(KERN_ERR "Bridge configure will happen.");

    switch (p_id->driver_data) {
        case PEX811X:	/* PLX PEX8111/PEX8112 PCIe/PCI bridge */
            pci_read_config_dword(bridge, 0x48, &tmp);
            tmp |= 1;	/* enable blind prefetching */
            tmp |= 1 << 11;	/* enable beacon generation */
            pci_write_config_dword(bridge, 0x48, tmp);

            pci_write_config_dword(bridge, 0x84, 0x0c);
            pci_read_config_dword(bridge, 0x88, &tmp);
            tmp &= ~(7 << 27);
            tmp |= 2 << 27;	/* set prefetch size to 128 bytes */
            pci_write_config_dword(bridge, 0x88, tmp);
            break;

        case PI7C9X110:	/* Pericom PI7C9X110 PCIe/PCI bridge */
            pci_read_config_dword(bridge, 0x40, &tmp);
            tmp |= 1;	/* park the PCI arbiter to the sound chip */
            pci_write_config_dword(bridge, 0x40, tmp);
            break;

        case XIO2001: /* Texas Instruments XIO2001 PCIe/PCI bridge */
            pci_read_config_dword(bridge, 0xe8, &tmp);
            tmp &= ~0xf;	/* request length limit: 64 bytes */
            tmp &= ~(0xf << 8);
            tmp |= 1 << 8;	/* request count limit: one buffer */
            pci_write_config_dword(bridge, 0xe8, tmp);
            break;
    }
}
