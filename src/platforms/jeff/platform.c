/*
 * This file is part of the Black Magic Debug project.
 *
 * Copyright (C) 2018  Flirc Inc.
 * Written by Jason Kotzin <jasonkotzin@gmail.com>
 *
 * This library is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "general.h"
#include "gdb_if.h"
#include "usb_serial.h"
#include "gdb_packet.h"

#include <libopencm3/sam/d/nvic.h>
#include <libopencm3/sam/d/port.h>
#include <libopencm3/sam/d/gclk.h>
#include <libopencm3/sam/d/pm.h>
#include <libopencm3/sam/d/uart.h>
#include <libopencm3/sam/d/adc.h>

#include <libopencm3/cm3/systick.h>
#include <libopencm3/cm3/scb.h>

#include <libopencm3/sam/d/tc.h>
#include <libopencm3/sam/d/eic.h>

static struct gclk_hw clock = {
	.gclk0 = SRC_DFLL48M,
	.gclk1 = SRC_OSC8M,
	.gclk1_div = 30,      /* divide clock for ADC  */
	.gclk2 = SRC_OSC8M,
	.gclk2_div = 100,     /* divide clock for TC */
	.gclk3 = SRC_DFLL48M,
	.gclk4 = SRC_DFLL48M,
	.gclk5 = SRC_DFLL48M,
	.gclk6 = SRC_DFLL48M,
	.gclk7 = SRC_DFLL48M,
};

extern void trace_tick(void);

uint8_t running_status;
static volatile uint32_t time_ms;

uint8_t button_pressed;

uint8_t tpwr_enabled;

void sys_tick_handler(void)
{
	if(running_status)
		gpio_toggle(LED_PORT, LED_IDLE_RUN);

	time_ms += 10;

	uart_pop();
}

uint32_t platform_time_ms(void)
{
	return time_ms;
}

static void usb_setup(void)
{
	/* Enable USB */
	INSERTBF(PM_APBBMASK_USB, 1, PM->apbbmask);

	/* enable clocking to usb */
	set_periph_clk(GCLK0, GCLK_ID_USB);
	periph_clk_en(GCLK_ID_USB, 1);

	gpio_set_af(PORTA, PORT_PMUX_FUN_G, GPIO24);
	gpio_set_af(PORTA, PORT_PMUX_FUN_G, GPIO25);

}

static uint32_t timing_init(void)
{
	uint32_t cal = 0;

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(4800);	/* Interrupt us at 10 Hz */
	systick_interrupt_enable();

	systick_counter_enable();
	return cal;
}

static void adc_init(void)
{
	gpio_set_af(ADC_PORT, PORT_PMUX_FUN_B, ADC_POS_PIN ); /* +input */
	gpio_set_af(ADC_PORT, PORT_PMUX_FUN_B, ADC_REF_PIN); /* reference */

	set_periph_clk(GCLK1, GCLK_ID_ADC);
	periph_clk_en(GCLK_ID_ADC, 1);

	adc_enable(ADC_REFCTRL_VREFA,0,ADC_INPUTCTRL_GND,ADC_MUXPOS);
}

static void counter_init(void)
{
	/* enable bus and clock */
	INSERTBF(PM_APBCMASK_TC3, 1, PM->apbcmask);

	set_periph_clk(GCLK2, GCLK_ID_TC3);
	periph_clk_en(GCLK_ID_TC3, 1);

	/* reset */
	tc_reset(3);

	/* set CTRLA.PRESCALER and CTRLA.PRESYNC */
	tc_config_ctrla(3,1,(7<<8));

	/* set CC0 (approx. 5 seconds delay) */
	tc_set_cc(3,0,1000);

	/* enable MC0 interrupt */
	tc_enable_interrupt(3,(1<<4));
	nvic_enable_irq(NVIC_TC3_IRQ);
}

static void button_init(void)
{
	gpio_set_af(BUTTON_PORT, PORT_PMUX_FUN_A , BUTTON_PIN );

	/* enable bus and clock */
	INSERTBF(PM_APBAMASK_EIC, 1, PM->apbamask);

	set_periph_clk(GCLK0, GCLK_ID_EIC);
	periph_clk_en(GCLK_ID_EIC, 1);

	/* configure r/f edge, enable filtering */
	eic_set_config(15, 1, EIC_FALL);

	/* enable the IEC */
	eic_enable(1);

	/* enable interrupts */
	eic_enable_interrupt((1<<15));
	nvic_enable_irq(NVIC_EIC_IRQ);
}

void platform_init(void)
{
	gclk_init(&clock);

	usb_setup();

	gpio_mode_setup(LED_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, LED_IDLE_RUN);
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, TMS_PIN);
	gpio_mode_setup(TCK_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, TCK_PIN);
	gpio_mode_setup(TDI_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, TDI_PIN);
	gpio_mode_setup(TMS_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, TMS_DIR_PIN);

	gpio_set(TMS_PORT, TMS_DIR_PIN);

	/* enable both input and output with pullup disabled by default */
	PORT_DIRSET(SWDIO_PORT) = SWDIO_PIN;
	PORT_PINCFG(SWDIO_PORT, SWDIO_PIN_NUM) |= PORT_PINCFG_INEN | PORT_PINCFG_PULLEN;
	gpio_clear(SWDIO_PORT, SWDIO_PIN);

	/* configure swclk_pin as output */
	gpio_mode_setup(SWCLK_PORT, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, SWCLK_PIN);
	gpio_clear(SWCLK_PORT, SWCLK_PIN);

	gpio_mode_setup(TDO_PORT, GPIO_MODE_INPUT, 0, TDO_PIN );
	gpio_mode_setup(SRST_PORT, GPIO_MODE_OUTPUT,  GPIO_CNF_PULLUP, SRST_PIN);

	gpio_clear(SRST_PORT, SRST_PIN);

	/* setup uart led, disable by default*/
	gpio_mode_setup(LED_PORT_UART, GPIO_MODE_OUTPUT, GPIO_CNF_PULLDOWN, LED_UART );	//GPIO_OUT_FLAG_DEFAULT_HIGH);

	gpio_clear(LED_PORT_UART, LED_UART);

	/* set up TPWR */
	gpio_set(PWR_BR_PORT, PWR_BR_PIN);
	gpio_mode_setup(PWR_BR_PORT, GPIO_MODE_OUTPUT,  GPIO_CNF_PULLUP, PWR_BR_PIN);

	timing_init();
//FIXME	usbuart_init();
//FIXME	cdcacm_init();
	adc_init();
	counter_init();
	button_init();
}

uint8_t srst_state;
void platform_srst_set_val(bool assert)
{
	volatile int i;
	if (!assert) {
		gpio_clear(SRST_PORT, SRST_PIN);
		for(i = 0; i < 10000; i++) __asm__ volatile("nop");
		srst_state = 0;
	} else {
		gpio_set(SRST_PORT, SRST_PIN);
		srst_state = 1;
	}
}

bool platform_srst_get_val(void)
{
	//return gpio_get(SRST_PORT, SRST_PIN) != 0;
	return srst_state;
}

bool platform_target_get_power(void)
{
	//return !gpio_get(PWR_BR_PORT, PWR_BR_PIN);
	return tpwr_enabled;
}

void platform_target_set_power(bool power)
{
	gpio_set_val(PWR_BR_PORT, PWR_BR_PIN, !power);
	tpwr_enabled = power;
}

void platform_delay(uint32_t ms)
{
	platform_timeout_s timeout;
	platform_timeout_set(&timeout, ms);
	while (!platform_timeout_is_expired(&timeout));
}

uint32_t platform_target_voltage_sense(void)
{
	uint32_t val;
	adc_start();

	while (!(1&(ADC->intflag)));
	val = ((485*adc_result())>>12); /* 330 without divider, 485 with it */
	return val;
}

const char *platform_target_voltage(void)
{
	uint32_t voltage;
	static char out[] = "0.0V";

	adc_start();

	voltage = platform_target_voltage_sense();

	out[0] = '0' + (char)(voltage/100);
	out[2] = '0' + (char)((voltage/10) % 10);

	return out;
}

char *serial_no_read(char *s, int max)
{
	if (max >= 8)
		max = 8;

#ifdef CUSTOM_SER
	s[0] = 'J';
	s[1] = 'E';
	s[2] = 'F';
	s[3] = 'F';
	return s;
#else
        int i;

	volatile uint32_t unique_id = *(volatile uint32_t *)0x0080A00C +
		*(volatile uint32_t *)0x0080A040 +
		*(volatile uint32_t *)0x0080A044 +
		*(volatile uint32_t *)0x0080A048;
	
        /* Fetch serial number from chip's unique ID */

        for(i = 0; i < max; i++) {
                s[7-i] = ((unique_id >> (4*i)) & 0xF) + '0';
        }

        for(i = 0; i < max; i++)
                if(s[i] > '9')
                        s[i] += 'A' - '9' - 1;
	s[8] = 0;

	return s;
#endif
}

void print_serial(void)
{
	gdb_outf("0x%08X%08X%08X%08X\n", *(volatile uint32_t *)0x0080A048,
			*(volatile uint32_t *)0x0080A044,
			*(volatile uint32_t *)0x0080A040,
			*(volatile uint32_t *)0x0080A00C);
}

void platform_request_boot(void)
{
}

void eic_isr(void)
{
	if (!button_pressed){
		/* set to rising-edge detection */
		eic_set_config(15, 1, EIC_RISE);
		
		/* enable counter */
		tc_enable(3,1);

		button_pressed = 1;
	} else {
		/* set to falling-edge detection */
		eic_set_config(15, 1, EIC_FALL);

		/* disable and reset counter */
		tc_enable(3,0);

		button_pressed = 0;
	}

	/* clear the interrupt */
	eic_clr_interrupt((1<<15));
}

void tc3_isr(void)
{
	if (tc_interrupt_flag(3) & 16)
		scb_reset_system();
}

uint32_t swd_delay_cnt = 0;

void platform_max_frequency_set(uint32_t freq)
{
	(void)freq;
}

uint32_t platform_max_frequency_get(void)
{
	return 0;
}
