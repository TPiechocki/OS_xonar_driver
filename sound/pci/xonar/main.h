//
// Created by Tomasz Piechocki on 16/11/2020.
//

#ifndef OS_MAIN_H
#define OS_MAIN_H

#include "oxygen.h"

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

#define OXYGEN_INTERRUPT_STATUS		0x46
#define OXYGEN_IO_SIZE	0x100

// main driver's card struct
struct xonar {
    // general PCI structure
    struct pci_dev *pci;
    // sound card structure from ALSA
    struct snd_card *card;
    // data connected with PCM (Pulse-Code Modulation) stream
    struct snd_pcm *pcm;
    struct snd_pcm_substream *substream;

    // hardware registers
    unsigned long ioport;
    // interrupt line number
    int irq;
    // interrupt mask which may be needed for interrupt handling
    unsigned int interrupt_mask;

    // TODO
    struct work_struct gpio_work;

    // hardware oxygen registers
    union {
        u8 _8[OXYGEN_IO_SIZE];
        __le16 _16[OXYGEN_IO_SIZE / 2];
        __le32 _32[OXYGEN_IO_SIZE / 4];
    } saved_registers;

    // hardware xonar elements
    unsigned int anti_pop_delay;
    u16 output_enable_bit;
    u8 ext_power_reg;
    u8 ext_power_int_reg;
    u8 ext_power_bit;
    u8 has_power;
    // front outputs DAC control registers
    u8 cs4398_regs[8];
    // DAC control registers for other outputs
    u8 cs4362a_regs[15];

    void (*gpio_changed)(struct xonar *chip);

    // general spinlock for e.g. interrupt handler
    struct spinlock lock;
};

// OXYGEN I/O operations exports
u8 xonar_read8(struct xonar *chip, unsigned int reg);
u16 xonar_read16(struct xonar *chip, unsigned int reg);
u32 xonar_read32(struct xonar *chip, unsigned int reg);

void oxygen_write8(struct xonar *chip, unsigned int reg, u8 value);
void oxygen_write16(struct xonar *chip, unsigned int reg, u16 value);
void oxygen_write32(struct xonar *chip, unsigned int reg, u32 value);

void oxygen_write8_masked(struct xonar *chip, unsigned int reg,
                          u8 value, u8 mask);
void oxygen_write16_masked(struct xonar *chip, unsigned int reg,
                           u16 value, u16 mask);
void oxygen_write32_masked(struct xonar *chip, unsigned int reg,
                           u32 value, u32 mask);

static inline void oxygen_set_bits8(struct oxygen *chip,
                                    unsigned int reg, u8 value) {
    oxygen_write8_masked(chip, reg, value, value);
}
static inline void oxygen_set_bits16(struct oxygen *chip,
                                     unsigned int reg, u16 value) {
    oxygen_write16_masked(chip, reg, value, value);
}
static inline void oxygen_set_bits32(struct oxygen *chip,
                                     unsigned int reg, u32 value) {
    oxygen_write32_masked(chip, reg, value, value);
}
static inline void oxygen_clear_bits8(struct oxygen *chip,
                                      unsigned int reg, u8 value) {
    oxygen_write8_masked(chip, reg, 0, value);
}
static inline void oxygen_clear_bits16(struct oxygen *chip,
                                       unsigned int reg, u16 value) {
    oxygen_write16_masked(chip, reg, 0, value);
}
static inline void oxygen_clear_bits32(struct oxygen *chip,
                                       unsigned int reg, u32 value) {
    oxygen_write32_masked(chip, reg, 0, value);
}


// xonar_hardware declarations


// xonar_lib helpers
#define GPI_EXT_POWER		0x01

void xonar_enable_output(struct xonar *chip);
void xonar_disable_output(struct xonar *chip);
void xonar_init_ext_power(struct xonar *chip);
void xonar_init_cs53x1(struct xonar *chip);
void xonar_set_cs53x1_params(struct xonar *chip,
                             struct snd_pcm_hw_params *params);

#define XONAR_GPIO_BIT_INVERT	(1 << 16)
int xonar_gpio_bit_switch_get(struct snd_kcontrol *ctl,
                              struct snd_ctl_elem_value *value);
int xonar_gpio_bit_switch_put(struct snd_kcontrol *ctl,
                              struct snd_ctl_elem_value *value);

#endif //OS_MAIN_H
