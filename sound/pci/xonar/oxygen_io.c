// SPDX-License-Identifier: GPL-2.0-only
/*
 * C-Media CMI8788 driver - helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

// INPUT and OTUPUT operations for the hardware data (registers)

#include <linux/delay.h>
#include <linux/sched.h>
#include <linux/export.h>
#include <linux/io.h>
#include <sound/core.h>
#include <sound/mpu401.h>

#include "oxygen_regs.h"
#include "main.h"

u8 xonar_read8(struct xonar *chip, unsigned int reg)
{
	return inb(chip->ioport + reg);
}
EXPORT_SYMBOL(xonar_read8);

u16 xonar_read16(struct xonar *chip, unsigned int reg)
{
	return inw(chip->ioport + reg);
}
EXPORT_SYMBOL(xonar_read16);

u32 xonar_read32(struct xonar *chip, unsigned int reg)
{
	return inl(chip->ioport + reg);
}
EXPORT_SYMBOL(xonar_read32);

void oxygen_write8(struct xonar *chip, unsigned int reg, u8 value)
{
	outb(value, chip->ioport + reg);
	chip->saved_registers._8[reg] = value;
}
EXPORT_SYMBOL(oxygen_write8);

void oxygen_write16(struct xonar *chip, unsigned int reg, u16 value)
{
	outw(value, chip->ioport + reg);
	chip->saved_registers._16[reg / 2] = cpu_to_le16(value);
}
EXPORT_SYMBOL(oxygen_write16);

void oxygen_write32(struct xonar *chip, unsigned int reg, u32 value)
{
	outl(value, chip->ioport + reg);
	chip->saved_registers._32[reg / 4] = cpu_to_le32(value);
}
EXPORT_SYMBOL(oxygen_write32);

void oxygen_write8_masked(struct xonar *chip, unsigned int reg,
                          u8 value, u8 mask)
{
    u8 tmp = inb(chip->ioport + reg);
    tmp &= ~mask;
    tmp |= value & mask;
    outb(tmp, chip->ioport + reg);
    chip->saved_registers._8[reg] = tmp;
}
EXPORT_SYMBOL(oxygen_write8_masked);

void oxygen_write16_masked(struct xonar *chip, unsigned int reg,
                           u16 value, u16 mask)
{
    u16 tmp = inw(chip->ioport + reg);
    tmp &= ~mask;
    tmp |= value & mask;
    outw(tmp, chip->ioport + reg);
    chip->saved_registers._16[reg / 2] = cpu_to_le16(tmp);
}
EXPORT_SYMBOL(oxygen_write16_masked);

void oxygen_write32_masked(struct xonar *chip, unsigned int reg,
                           u32 value, u32 mask)
{
    u32 tmp = inl(chip->ioport + reg);
    tmp &= ~mask;
    tmp |= value & mask;
    outl(tmp, chip->ioport + reg);
    chip->saved_registers._32[reg / 4] = cpu_to_le32(tmp);
}
EXPORT_SYMBOL(oxygen_write32_masked);


// I2C

void oxygen_write_i2c(struct xonar *chip, u8 device, u8 map, u8 data)
{
    /* should not need more than about 300 us */
    msleep(1);

    oxygen_write8(chip, OXYGEN_2WIRE_MAP, map);
    oxygen_write8(chip, OXYGEN_2WIRE_DATA, data);
    oxygen_write8(chip, OXYGEN_2WIRE_CONTROL,
                  device | OXYGEN_2WIRE_DIR_WRITE);
}
EXPORT_SYMBOL(oxygen_write_i2c);


// AC97 I/O

/*
 * About 10% of AC'97 register reads or writes fail to complete, but even those
 * where the controller indicates completion aren't guaranteed to have actually
 * happened.
 *
 * It's hard to assign blame to either the controller or the codec because both
 * were made by C-Media ...
 */

static int oxygen_ac97_wait(struct xonar *chip, unsigned int mask)
{
    u8 status = 0;

    /*
     * Reading the status register also clears the bits, so we have to save
     * the read bits in status.
     */
    wait_event_timeout(chip->ac97_waitqueue,
                       ({ status |= xonar_read8(chip, OXYGEN_AC97_INTERRUPT_STATUS);
                           status & mask; }),
                       msecs_to_jiffies(1) + 1);
    /*
     * Check even after a timeout because this function should not require
     * the AC'97 interrupt to be enabled.
     */
    status |= xonar_read8(chip, OXYGEN_AC97_INTERRUPT_STATUS);
    return status & mask ? 0 : -EIO;
}


void oxygen_write_ac97(struct xonar *chip, unsigned int codec,
                       unsigned int index, u16 data)
{
    unsigned int count, succeeded;
    u32 reg;

    reg = data;
    reg |= index << OXYGEN_AC97_REG_ADDR_SHIFT;
    reg |= OXYGEN_AC97_REG_DIR_WRITE;
    reg |= codec << OXYGEN_AC97_REG_CODEC_SHIFT;
    succeeded = 0;
    for (count = 5; count > 0; --count) {
        udelay(5);
        oxygen_write32(chip, OXYGEN_AC97_REGS, reg);
        /* require two "completed" writes, just to be sure */
        if (oxygen_ac97_wait(chip, OXYGEN_AC97_INT_WRITE_DONE) >= 0 &&
            ++succeeded >= 2) {
            chip->saved_ac97_registers[codec][index / 2] = data;
            return;
        }
    }
    dev_err(chip->card->dev, "AC'97 write timeout\n");
}
EXPORT_SYMBOL(oxygen_write_ac97);

u16 oxygen_read_ac97(struct xonar *chip, unsigned int codec,
                     unsigned int index)
{
    unsigned int count;
    unsigned int last_read = UINT_MAX;
    u32 reg;

    reg = index << OXYGEN_AC97_REG_ADDR_SHIFT;
    reg |= OXYGEN_AC97_REG_DIR_READ;
    reg |= codec << OXYGEN_AC97_REG_CODEC_SHIFT;
    for (count = 5; count > 0; --count) {
        udelay(5);
        oxygen_write32(chip, OXYGEN_AC97_REGS, reg);
        udelay(10);
        if (oxygen_ac97_wait(chip, OXYGEN_AC97_INT_READ_DONE) >= 0) {
            u16 value = xonar_read16(chip, OXYGEN_AC97_REGS);
            /* we require two consecutive reads of the same value */
            if (value == last_read)
                return value;
            last_read = value;
            /*
             * Invert the register value bits to make sure that two
             * consecutive unsuccessful reads do not return the same
             * value.
             */
            reg ^= 0xffff;
        }
    }
    dev_err(chip->card->dev, "AC'97 read timeout on codec %u\n", codec);
    return 0;
}
EXPORT_SYMBOL(oxygen_read_ac97);

void oxygen_write_ac97_masked(struct xonar *chip, unsigned int codec,
                              unsigned int index, u16 data, u16 mask)
{
    u16 value = oxygen_read_ac97(chip, codec, index);
    value &= ~mask;
    value |= data & mask;
    oxygen_write_ac97(chip, codec, index, value);
}
EXPORT_SYMBOL(oxygen_write_ac97_masked);