/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* 	Command: calibration
		Arguments: [force]

		Description: launches RX timestamper calibration. */

#include <string.h>
#include <wrc.h>
#include "shell.h"
#include "eeprom.h"
#include "syscon.h"
#include "rxts_calibrator.h"

static int cmd_calibration(const char *args[])
{
	uint32_t trans;

	if (args[0] && !strcasecmp(args[0], "force")) {
		if (measure_t24p(&trans) < 0)
			return -1;
		return eeprom_phtrans(WRPC_FMC_I2C, FMC_EEPROM_ADR, &trans, 1);
	} else if (!strcmp(args[0],"sweep"))
		{
			 calib_t24p_full_sweep();

		}else if (!args[0]) {
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

DEFINE_WRC_COMMAND(calibration) = {
	.name = "calibration",
	.exec = cmd_calibration,
};
