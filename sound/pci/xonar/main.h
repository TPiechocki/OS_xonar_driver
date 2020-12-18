//
// Created by Tomasz Piechocki on 16/11/2020.
//

#ifndef OS_MAIN_H
#define OS_MAIN_H

#include <sound/control.h>

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
    u16 saved_ac97_registers[2][0x40];

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


    // OXYGEN - don't really know the meaning of things here
    u8 dac_volume[8];
    u8 dac_mute;
    u8 pcm_active;
    u8 pcm_running;
    u8 dac_routing;
    u8 spdif_playback_enable;
    u8 has_ac97_0;
    u8 has_ac97_1;
    u32 spdif_bits;
    u32 spdif_pcm_bits;
    wait_queue_head_t ac97_waitqueue;

    // oxygen->model
    size_t model_data_size;
    unsigned int device_config;
    u8 dac_channels_pcm;
    u8 dac_channels_mixer;
    u8 dac_volume_min;
    u8 dac_volume_max;
    u8 misc_flags;
    u8 function_flags;
    u8 dac_mclks;
    u8 adc_mclks;
    u16 dac_i2s_format;
    u16 adc_i2s_format;
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

static inline void oxygen_set_bits8(struct xonar *chip,
                                    unsigned int reg, u8 value) {
    oxygen_write8_masked(chip, reg, value, value);
}
static inline void oxygen_set_bits16(struct xonar *chip,
                                     unsigned int reg, u16 value) {
    oxygen_write16_masked(chip, reg, value, value);
}
static inline void oxygen_set_bits32(struct xonar *chip,
                                     unsigned int reg, u32 value) {
    oxygen_write32_masked(chip, reg, value, value);
}
static inline void oxygen_clear_bits8(struct xonar *chip,
                                      unsigned int reg, u8 value) {
    oxygen_write8_masked(chip, reg, 0, value);
}
static inline void oxygen_clear_bits16(struct xonar *chip,
                                       unsigned int reg, u16 value) {
    oxygen_write16_masked(chip, reg, 0, value);
}
static inline void oxygen_clear_bits32(struct xonar *chip,
                                       unsigned int reg, u32 value) {
    oxygen_write32_masked(chip, reg, 0, value);
}

void oxygen_write_i2c(struct xonar *chip, u8 device, u8 map, u8 data);


u16 oxygen_read_ac97(struct xonar *chip, unsigned int codec,
                     unsigned int index);
void oxygen_write_ac97(struct xonar *chip, unsigned int codec,
                       unsigned int index, u16 data);
void oxygen_write_ac97_masked(struct xonar *chip, unsigned int codec,
                              unsigned int index, u16 data, u16 mask);
static inline void oxygen_ac97_set_bits(struct xonar *chip, unsigned int codec,
                                        unsigned int index, u16 value)
{
    oxygen_write_ac97_masked(chip, codec, index, value, value);
}

static inline void oxygen_ac97_clear_bits(struct xonar *chip,
                                          unsigned int codec,
                                          unsigned int index, u16 value)
{
    oxygen_write_ac97_masked(chip, codec, index, 0, value);
}


// xonar_hardware declarations
void xonar_dx_init(struct xonar *chip);
void xonar_dx_cleanup(struct xonar *chip);
// set internal DACs control registers
void set_cs43xx_params(struct xonar *chip, struct snd_pcm_hw_params *params);

// and oxygen hardware as well
static void oxygen_init(struct xonar *chip);

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


// OXYGEN DEFINES
/* 1 << PCM_x == OXYGEN_CHANNEL_x */
#define PCM_A		0
#define PCM_B		1
#define PCM_C		2
#define PCM_SPDIF	3
#define PCM_MULTICH	4
#define PCM_AC97	5
#define PCM_COUNT	6

#define OXYGEN_MCLKS(f_single, f_double, f_quad) ((MCLK_##f_single << 0) | \
						  (MCLK_##f_double << 2) | \
						  (MCLK_##f_quad   << 4))

#define OXYGEN_IO_SIZE	0x100

#define OXYGEN_EEPROM_ID	0x434d	/* "CM" */

/* model-specific configuration of outputs/inputs */
#define PLAYBACK_0_TO_I2S	0x0001
/* PLAYBACK_0_TO_AC97_0		not implemented */
#define PLAYBACK_1_TO_SPDIF	0x0004
#define PLAYBACK_2_TO_AC97_1	0x0008
#define CAPTURE_0_FROM_I2S_1	0x0010
#define CAPTURE_0_FROM_I2S_2	0x0020
/* CAPTURE_0_FROM_AC97_0		not implemented */
#define CAPTURE_1_FROM_SPDIF	0x0080
#define CAPTURE_2_FROM_I2S_2	0x0100
#define CAPTURE_2_FROM_AC97_1	0x0200
#define CAPTURE_3_FROM_I2S_3	0x0400
#define MIDI_OUTPUT		0x0800
#define MIDI_INPUT		0x1000
#define AC97_CD_INPUT		0x2000
#define AC97_FMIC_SWITCH	0x4000


#endif //OS_MAIN_H
