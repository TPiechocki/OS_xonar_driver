//
// Created by Tomasz Piechocki on 16/11/2020.
//

#ifndef OS_MAIN_H
#define OS_MAIN_H

// card name for module parameters
#define CARD_NAME "Xonar DX"

#define PCI_DEV_ID_CM8788 0x8788
#define PCI_VENDOR_ID_ASUS 0x1043
#define PCI_DEV_ID_XONARDX 0x8275

// main driver's card struct
struct xonar {
    struct snd_card *card;
    struct pci_dev *pci;

    unsigned long ioport;
    int irq;
};

#endif //OS_MAIN_H
