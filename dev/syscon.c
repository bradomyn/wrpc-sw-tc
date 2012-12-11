/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#include "syscon.h"
#include "i2c.h"

/* access functions for the bit banged I2C busses */
static int fmc_sda_io(int state)
{
	int value = gpio_in(SYSC_GPSR_FMC_SDA);
	gpio_out(SYSC_GPSR_FMC_SDA, state);
	return value;
}

static int fmc_scl_io(int state)
{
	int value = gpio_in(SYSC_GPSR_FMC_SCL);
	gpio_out(SYSC_GPSR_FMC_SCL, state);
	return value;
}

static int sfp_sda_io(int state)
{
	int value = gpio_in(SYSC_GPSR_SFP_SDA);
	gpio_out(SYSC_GPSR_SFP_SDA, state);
	return value;
}

static int sfp_scl_io(int state)
{
	int value = gpio_in(SYSC_GPSR_SFP_SCL);
	gpio_out(SYSC_GPSR_SFP_SCL, state);
	return value;
}

static const struct i2c_bitbang_interface bb_i2c_fmc = { fmc_scl_io, fmc_sda_io };
static const struct i2c_bitbang_interface bb_i2c_sfp = { sfp_scl_io, sfp_sda_io };

volatile struct SYSCON_WB *syscon;

/****************************
 *        TIMER
 ***************************/

void timer_init(uint32_t enable)
{
	syscon = (volatile struct SYSCON_WB *)BASE_SYSCON;

	if (enable)
		syscon->TCR |= SYSC_TCR_ENABLE;
	else
		syscon->TCR &= ~SYSC_TCR_ENABLE;
}

uint32_t timer_get_tics()
{
	return syscon->TVR;
}

void timer_delay(uint32_t how_long)
{
	uint32_t t_start;

//  timer_init(1);
	do {
		t_start = timer_get_tics();
	} while (t_start > UINT32_MAX - how_long);	//in case of overflow

	while (t_start + how_long > timer_get_tics()) ;
}

void syscon_init()
{
	mi2c_register_interface(WRPC_FMC_I2C, &bb_i2c_fmc);
	mi2c_register_interface(WRPC_SFP_I2C, &bb_i2c_sfp);

	timer_init(1);
}
