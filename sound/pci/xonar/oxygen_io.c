// SPDX-License-Identifier: GPL-2.0-only
/*
 * C-Media CMI8788 driver - helper functions
 *
 * Copyright (c) Clemens Ladisch <clemens@ladisch.de>
 */

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
/*
void xonar_write8(struct xonar *chip, unsigned int reg, u8 value)
{
	outb(value, chip->ioport + reg);
	chip->saved_registers._8[reg] = value;
}
EXPORT_SYMBOL(xonar_write8);

void xonar_write16(struct xonar *chip, unsigned int reg, u16 value)
{
	outw(value, chip->ioport + reg);
	chip->saved_registers._16[reg / 2] = cpu_to_le16(value);
}
EXPORT_SYMBOL(xonar_write16);

void xonar_write32(struct xonar *chip, unsigned int reg, u32 value)
{
	outl(value, chip->ioport + reg);
	chip->saved_registers._32[reg / 4] = cpu_to_le32(value);
}
EXPORT_SYMBOL(xonar_write32);
*/