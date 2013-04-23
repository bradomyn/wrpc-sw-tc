/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* 	Command: gui
		Arguments: none

		Description: launches the WR Core monitor GUI */

#include <string.h>
#include <wrc.h>
#include "shell.h"
#include "eeprom.h"
#include "w1.h"
#include "syscon.h"

extern int measure_t24p(uint32_t * value);

/*I2C bus command*/
#ifdef CONFIG_EEPROM_I2C
static int cmd_calibration(const char *args[])
{
	uint32_t trans;

	if (args[0] && !strcasecmp(args[0], "force")) {
		if (measure_t24p(&trans) < 0)
			return -1;
		return eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, &trans, 1);
	} else if (!args[0]) {
		if (eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, &trans, 0) > 0) {
			mprintf("Found phase transition in EEPROM: %dps\n",
				trans);
			cal_phase_transition = trans;
			return 0;
		} else {
			mprintf("Measuring t2/t4 phase transition...\n");
			if (measure_t24p(&trans) < 0)
				return -1;
			cal_phase_transition = trans;
			return eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR,
					      &trans, 1);
		}
	}

	return 0;
}

#endif

/*1Wire bus command*/
#ifdef CONFIG_EEPROM_W1
static int cmd_calibration(const char *args[])
{
	uint32_t trans;
	int temp;

	if (args[0] && !strcasecmp(args[0], "force")) {
		if (measure_t24p(&trans) < 0) {
			pp_printf("Error in the Calibrarion \n");
			return -1;
		}
		if (w1_eeprom_phtrans(&trans, 1) < 0) {
			pp_printf("Error writing calibration in EEPROM \n");
			return -1;
		}

		return 0;

	} else if (!args[0]) {
		temp = w1_eeprom_phtrans(&trans, 0);

		if (temp > 0) {
			pp_printf("Found phase transition in EEPROM: %dps\n",
				  trans);
			cal_phase_transition = trans;
			return 0;
		} else if (temp == 0) {
			mprintf("Measuring t2/t4 phase transition...\n");
			if (measure_t24p(&trans) < 0)
				return -1;

			cal_phase_transition = trans;

			if (w1_eeprom_phtrans(&trans, 1) < 0) {
				pp_printf
				    ("Error writing calibration in EEPROM \n");
				return -1;
			}
			return 0;
		}
	} else {
		pp_printf("Error reading in EEPROM\n");
		return -1;
	}

	return 0;
}
#endif

DEFINE_WRC_COMMAND(calibration) = {
	.name = "calibration",
	.exec = cmd_calibration,
};
