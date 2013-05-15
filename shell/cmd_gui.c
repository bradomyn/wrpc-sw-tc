/* 	Command: gui
		Arguments: none

		Description: launches the WR Core monitor GUI */

#include <string.h>
#include "shell.h"

static int cmd_gui(const char *args[])
{
	wrc_ui_mode = UI_GUI_MODE;
	if(args[0] && !strcasecmp(args[0], "simple"))
		wrc_ui_mode = UI_SGUI_MODE;

	return 0;
}

DEFINE_WRC_COMMAND(gui) = {
	.name = "gui",
	.exec = cmd_gui,
};
