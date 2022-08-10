/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2016  Black Sphere Technologies Ltd.
 * Written by Gareth McMullin <gareth@blacksphere.co.nz>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include "general.h"

static uint8_t olddir = 0;

static inline void swdptap_set_out(void)
{
	/* Don't turnaround if direction not changing */
	if(0 == olddir) return;
	olddir = 0;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> ":"\n<- ");
#endif

	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
	SWDIO_MODE_DRIVE();
}


static inline void swdptap_set_in(void)
{
	/* Don't turnaround if direction not changing */
	if(1 == olddir) return;
	olddir = 1;

#ifdef DEBUG_SWD_BITS
	DEBUG("%s", dir ? "\n-> ":"\n<- ");
#endif

	SWDIO_MODE_FLOAT();
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}

static inline bool swdptap_bit_in(void)
{
	uint16_t ret;

	ret = gpio_get(SWDIO_PORT, SWDIO_PIN);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);

#ifdef DEBUG_SWD_BITS
	DEBUG("%d", ret?1:0);
#endif

	return ret != 0;
}

static inline void swdptap_bit_out(bool val)
{
#ifdef DEBUG_SWD_BITS
	DEBUG("%d", val);
#endif

	gpio_set_val(SWDIO_PORT, SWDIO_PIN, val);
	gpio_set(SWCLK_PORT, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);
}

uint32_t __attribute__((weak))
swdptap_seq_in(int ticks)
{
	uint32_t index = 1;
	uint32_t ret = 0;

	swdptap_set_in();

	while (ticks--) {
		if (swdptap_bit_in())
			ret |= index;
		index <<= 1U;
	}

	return ret;
}

bool swdptap_seq_in_parity(uint32_t *ret, int ticks)
{
	uint32_t index = 1;
	uint8_t parity = 0;
	*ret = 0;

	swdptap_set_in();

	while (ticks--) {
		if (swdptap_bit_in()) {
			*ret |= index;
			parity ^= 1U;
		}
		index <<= 1U;
	}
	if (swdptap_bit_in())
		parity ^= 1U;

	return parity;
}

void swdptap_seq_out(uint32_t MS, int ticks)
{
	swdptap_set_out();

	while (ticks--) {
		swdptap_bit_out(MS & 1U);
		MS >>= 1U;
	}
}

void swdptap_seq_out_parity(uint32_t MS, int ticks)
{
	uint8_t parity = 0;

	swdptap_set_out();

	while (ticks--) {
		swdptap_bit_out(MS & 1U);
		parity ^= MS;
		MS >>= 1U;
	}
	swdptap_bit_out(parity & 1U);
}

swd_proc_t swd_proc;

int swdptap_init(void)
{
	swd_proc.swdptap_seq_in  = swdptap_seq_in;
	swd_proc.swdptap_seq_in_parity  = swdptap_seq_in_parity;
	swd_proc.swdptap_seq_out = swdptap_seq_out;
	swd_proc.swdptap_seq_out_parity  = swdptap_seq_out_parity;

	return 0;
}
