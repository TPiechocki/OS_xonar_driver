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
