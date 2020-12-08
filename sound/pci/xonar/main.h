//
// Created by Tomasz Piechocki on 16/11/2020.
//

#ifndef OS_MAIN_H
#define OS_MAIN_H

#include <sound/pci/ox

// card name for module parameters
#define CARD_NAME "Xonar DX"

// ids of subdevice, subvendor, subdevice
#define PCI_DEV_ID_CM8788 0x8788
#define PCI_VENDOR_ID_ASUS 0x1043
#define PCI_DEV_ID_XONARDX 0x8275

// buffer limit sizes for pcm stream
#define PERIOD_BYTES_MIN		64
/* most DMA channels have a 16-bit counter for 32-bit words */
#define BUFFER_BYTES_MAX		((1 << 16) * 4)
/* the multichannel DMA channel has a 24-bit counter */
#define BUFFER_BYTES_MAX_MULTICH	((1 << 24) * 4)

#define DEFAULT_BUFFER_BYTES		(BUFFER_BYTES_MAX / 2)
#define DEFAULT_BUFFER_BYTES_MULTICH	(1024 * 1024)


// main driver's card struct
struct xonar {
    unsigned long addr;
    struct snd_card *card;
    struct pci_dev *pci;

    unsigned long ioport;
    int irq;

    struct snd_pcm *pcm;
    struct snd_pcm_substream *substream;

    struct spinlock lock;
};

#endif //OS_MAIN_H
