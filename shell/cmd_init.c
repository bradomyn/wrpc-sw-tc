/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#include <string.h>
#include <wrc.h>
#include "shell.h"
#include "eeprom.h"
#include "w1.h"
#include "syscon.h"
#include "i2c.h"

#ifdef CONFIG_EEPROM_I2C
static int cmd_init(const char *args[])
{
	if (!mi2c_devprobe(WRPC_FMC_I2C, FMC_EEPROM_ADR)) {
		mprintf("EEPROM not found..\n");
		return -1;
	}

	if (args[0] && !strcasecmp(args[0], "erase")) {
		if (eeprom_init_erase(WRPC_FMC_I2C, FMC_EEPROM_ADR) < 0)
			mprintf("Could not erase init script\n");
	} else if (args[0] && !strcasecmp(args[0], "purge")) {
		eeprom_init_purge(WRPC_FMC_I2C, FMC_EEPROM_ADR);
	} else if (args[1] && !strcasecmp(args[0], "add")) {
		if (eeprom_init_add(WRPC_FMC_I2C, FMC_EEPROM_ADR, args) < 0)
			mprintf("Could not add the command\n");
		else
			mprintf("OK.\n");
	} else if (args[0] && !strcasecmp(args[0], "show")) {
		eeprom_init_show(WRPC_FMC_I2C, FMC_EEPROM_ADR);
	} else if (args[0] && !strcasecmp(args[0], "boot")) {
		shell_boot_script();
	}

	return 0;
}
#endif

#ifdef CONFIG_EEPROM_W1
static int cmd_init(const char *args[])
{
	if (args[0] && !strcasecmp(args[0], "erase")) {
		if (w1_eeprom_init_erase() < 0)
			mprintf("Could not erase init script\n");
	} else if (args[0] && !strcasecmp(args[0], "purge")) {
		/*no purge function */
	} else if (args[1] && !strcasecmp(args[0], "add")) {

		if (w1_eeprom_init_add(args) < 0)
			mprintf("Could not add the command\n");
		else
			mprintf("Command added\n");
	} else if (args[0] && !strcasecmp(args[0], "show")) {
		if (w1_eeprom_init_show() < 0)
			pp_printf("Error reading EEPROM\n");
	} else if (args[0] && !strcasecmp(args[0], "boot")) {
		shell_boot_script();
	}

	return 0;
}
#endif

DEFINE_WRC_COMMAND(init) = {
.name = "init",.exec = cmd_init,};
