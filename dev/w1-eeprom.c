/*
 * Eeprom support
 * Cesar Prados, 2013 GNU GPL2 or later
 */
#include <stdlib.h>
#include <string.h>
#include <wrc.h>
#include <shell.h>
#include <w1.h>
#include <eeprom.h>

/*
 * The OneWire EEPROM contains important information for White Rabbit
 * protocol: Transition Phase and SFP calibration parameters,
 * networking: mac addresse and an init script area consist of 2-byte
 * size field and a set of shell commands.
 *
 * The structure of SFP section is:
 *
 *        ------------------
 * 0x0    | mac address (6B)|
 *        ------------------------------------
 * 0x200  | cal_ph_trans (4B) | SFP count (1B)|
 *        -------------------------------------------------------------------
 * 0x240  | SFP(1) partnumber (16B) | alpha (4B) | deltaTx(4B) | deltaRx(4B) |
 *        -------------------------------------------------------------------
 *        | SFP(2) partnumber (16B) | alpha (4B) | deltaTx(4B) | deltaRx(4B) |
 *        -------------------------------------------------------------------
 *        | (....)                  | (....)     | (....)      | (....)      |
 *        -------------------------------------------------------------------
 * 0x400  | bytes used (2B) |
 *        -------------------------------------------------------------------
 *       | init script                                                       |
 *       --------------------------------------------------------------------
 *
 * Fields description:
 *
 * cal_ph_trans       - t2/t4 phase transition value (from measure_t24p() ),
 *                      contains _valid_ bit (MSB) and 31 bits of
 *                      cal_phase_transition value
 *
 * count              - how many SFPs are described in the list (binary)
 *
 * SFP(n) part number - SFP PN as read from SFP's EEPROM (e.g. AXGE-1254-0531)
 *                      (16 ascii chars)
 */
#define LSB_ADDR(X) ((X) & 0xFF)
#define MSB_ADDR(X) (((X) & 0xFF00)>>8)
uint8_t has_w1_eeprom;

int w1_write_eeprom(struct w1_dev *dev, int offset, const uint8_t *buffer,
		    int blen)
{
	int i;
	int read_data;

	w1_match_rom(dev);
	w1_write_byte(dev->bus, W1_CMDR_W_SPAD);

	/* write of target addr */
	w1_write_byte(dev->bus, LSB_ADDR(offset));
	w1_write_byte(dev->bus, MSB_ADDR(offset));

	/* write to scratchpad */
	for (i = 0; i < blen; i++)
		w1_write_byte(dev->bus, buffer[i]);
	for (; i < 32; i++)
		w1_write_byte(dev->bus, '\0');	/*padding */

	w1_match_rom(dev);
	w1_write_byte(dev->bus, W1_CMDR_C_SPAD);

	/* copy to memory */
	w1_write_byte(dev->bus, LSB_ADDR(offset));
	w1_write_byte(dev->bus, MSB_ADDR(offset));

	/* ending offset/data status byte */
	w1_write_byte(dev->bus, 0x1f);
	usleep(10000);

	/* check final status byte */
	read_data = w1_read_byte(dev->bus);
	if (read_data != 0xaa)
		return -1;
	return 0;
}

int w1_read_eeprom(struct w1_dev *dev, int offset, uint8_t *buffer, int blen)
{

	int i;

	w1_match_rom(dev);
	w1_write_byte(dev->bus, W1_CMDR_R_MEMORY);

	/* read of target addr */
	w1_write_byte(dev->bus, LSB_ADDR(offset));
	w1_write_byte(dev->bus, MSB_ADDR(offset));

	for (i = 0; i < blen; i++)
		buffer[i] = w1_read_byte(dev->bus);

	return 0;
}

/* read in the first ow eeprom in the bus */
int w1_read_eeprom_bus(struct w1_bus *bus, int offset, uint8_t *buffer,
		       int blen)
{
	int i, class;

	for (i = 0; i < W1_MAX_DEVICES; i++) {
		class = w1_class(bus->devs + i);

		/* familiy class of the eeprom */
		switch (class) {
		case 0x43:
			return w1_read_eeprom(bus->devs + i, offset, buffer,
					      blen);
		default:
			break;
		}
	}

	return -1;
}

/* check if there is a eeprom in the 1-wire network */
int w1_eeprom_present(struct w1_bus *bus)
{
	int i, class;
	has_w1_eeprom = 0;

	for (i = 0; i < W1_MAX_DEVICES; i++) {
		class = w1_class(bus->devs + i);

		/* familiy class of the eeprom */
		switch (class) {
		case 0x43:
			has_w1_eeprom = 1;
			return 0;
		default:
			break;
		}
	}

	return 1;
}

/* write in the first ow eeprom in the bus */
int w1_write_eeprom_bus(struct w1_bus *bus, int offset, const uint8_t *buffer,
			int blen)
{
	int i, class;

	for (i = 0; i < W1_MAX_DEVICES; i++) {
		class = w1_class(bus->devs + i);

		/* familiy class of the eeprom */
		switch (class) {
		case 0x43:
			return w1_write_eeprom(bus->devs + i, offset, buffer,
					       blen);
		default:
			break;
		}
	}

	return -1;
}

/* erase the complete sfp data base */
int w1_eemprom_sfpdb_erase()
{
	uint8_t buf[W1_BUF];
	int n;

	if (w1_read_eeprom_bus(&wrpc_w1_bus, EE_BASE_SFP, buf, W1_BUF) < 0)
		return -1;
	n = buf[4];
	buf[4] = 0;		/*| cal_ph_trans (4B) | SFP count (1B) | */
	if (w1_write_eeprom_bus(&wrpc_w1_bus, EE_BASE_SFP, buf, W1_BUF) < 0) {
		pp_printf("Error erasing SFP DB\n");
		return -1;
	}

	pp_printf("%d SFPs in DB erased\n", n);
	return 0;
}

/* add/show sfps in the data base */
int w1_eeprom_get_sfp(struct s_sfpinfo *sfp, uint8_t add, uint8_t pos)
{
	uint8_t sfpc = 0;
	uint8_t i, chksum = 0;
	uint8_t *ptr;
	uint8_t buf[W1_BUF];

	/* read how many SFPs are in the database when pos=0 */
	if (w1_read_eeprom_bus (&wrpc_w1_bus, EE_BASE_SFP, buf, W1_BUF) < 0)
		return EE_RET_1WERR;

	sfpc = buf[4];
	//mprintf("Number of SFP in data base %d \n", sfpc);

	if (add && (sfpc == SFPS_MAX))	/* no more space  for new SFPs */
		return EE_RET_DBFULL;
	else if (!pos && !add && (sfpc == 0))	/* no SFPs in db */
		return sfpc;

	/* showing SFPs in the DB */
	if (!add) {
		ptr = (uint8_t *) sfp;

		if (w1_read_eeprom_bus(&wrpc_w1_bus,
				       ((pos + 1) * EE_BASE_CAL + EE_BASE_SFP),
				       ptr, W1_BUF) < 0)
			return EE_RET_1WERR;

		for (i = 0; i < 4; ++i)
			chksum =
			    (uint8_t) ((uint16_t) chksum + *(ptr++)) & 0xff;

	} else {		/* adding spf at the end of the db */
		if (w1_write_eeprom_bus
		    (&wrpc_w1_bus, ((sfpc + 1) * EE_BASE_CAL) + EE_BASE_SFP,
		     (uint8_t *) sfp, W1_BUF) < 0)
			return EE_RET_1WERR;

		sfpc++;
		buf[4] = sfpc;
		if (w1_write_eeprom_bus
		    (&wrpc_w1_bus, EE_BASE_SFP, (uint8_t *) buf, W1_BUF) < 0)
			return EE_RET_1WERR;
	}
	return sfpc;
}

/* macht a given sfp against the db of sfp */
int w1_eeprom_match_sfp(struct s_sfpinfo *sfp)
{
	uint8_t sfpcount = 1;
	int8_t i, temp;
	struct s_sfpinfo dbsfp;
	for (i = 0; i < sfpcount; ++i) {
		temp = w1_eeprom_get_sfp(&dbsfp, 0, i);

		if (!i) {
			sfpcount = temp;
			/* only in first round valid sfpcount */
			if (sfpcount == 0 || sfpcount == 0xFF)
				return 0;
			else if (sfpcount < 0)
				return sfpcount;
		}
		if (!strncmp(dbsfp.pn, sfp->pn, 16)) {
			sfp->dTx = dbsfp.dTx;
			sfp->dRx = dbsfp.dRx;
			sfp->alpha = dbsfp.alpha;
			return 1;
		}
	}
	return 0;
}

/* write/read the phase transition parameter */
int w1_eeprom_phtrans(uint32_t * val, uint8_t write)
{
	uint8_t buf[W1_BUF];
	if (write) {

		*val |= (1 << 31);
		if (w1_read_eeprom_bus(&wrpc_w1_bus, EE_BASE_SFP, buf,
				       W1_BUF) < 0)
			return EE_RET_1WERR;

		memcpy(buf, val, 4);

		if (w1_write_eeprom_bus(&wrpc_w1_bus, EE_BASE_SFP, buf,
					W1_BUF) < 0)
			return EE_RET_1WERR;
		return 0;
	}
	if (w1_read_eeprom_bus(&wrpc_w1_bus, EE_BASE_SFP, buf, W1_BUF) < 0)
		return EE_RET_1WERR;

	memcpy(val, buf, 4);

	if (!(*val & (1 << 31)))
		return 0;
	*val &= 0x7fffffff;	/*return ph_trans value without validity bit */
	return 1;
}

/* clean up the init script */
int w1_eeprom_init_erase()
{
	uint8_t buf[W1_BUF];
	buf[0] = 0xff;

	if (w1_write_eeprom_bus(&wrpc_w1_bus, EE_BASE_INIT, buf, W1_BUF) < 0)
		return EE_RET_1WERR;

	pp_printf("Erase db of init\n");
	return 0;
}

/* add commands to the init script */
int w1_eeprom_init_add(const char *args[])
{
	uint8_t i = 1;
	uint8_t buf[W1_BUF];
	uint8_t num_cmd[W1_BUF];
	uint16_t used;
	if (w1_read_eeprom_bus(&wrpc_w1_bus, EE_BASE_INIT, num_cmd, W1_BUF) < 0)
		return EE_RET_1WERR;
	used = num_cmd[0];
	if (used == 0xff)
		used = 0;	/*this means the memory is blank */

	while (args[i] != '\0') {
		if (strlen((char *)buf) + strlen((char *)args[i]) > W1_BUF) {
			pp_printf("Sorry but the command is too large \n");
			return EE_RET_1WERR;
		}
		strcat((char *)buf, (char *)args[i]);
		strcat((char *)buf, " ");
		i++;
	}
	buf[strlen((char *)buf) - 1] = '\n';	/*replace last separator */

	if (w1_write_eeprom_bus
	    (&wrpc_w1_bus, EE_BASE_INIT + (EE_BASE_CAL * (used + 1)), buf,
	     W1_BUF) < 0)
		return EE_RET_1WERR;
	used++;
	pp_printf("%d commands in init script \n", used);
	num_cmd[0] = used;	/* update the number of scripts */
	if (w1_write_eeprom_bus(&wrpc_w1_bus, EE_BASE_INIT, num_cmd,
				W1_BUF) < 0)
		return EE_RET_1WERR;
	return 0;
}

/* show the content of the init script*/
int w1_eeprom_init_show()
{
	uint8_t buf[W1_BUF];
	uint16_t used, i, j;

	if (w1_read_eeprom_bus(&wrpc_w1_bus, EE_BASE_INIT, buf, W1_BUF) < 0)
		return EE_RET_1WERR;

	used = (uint16_t) buf[0];
	if (used == 0xff) {
		used = 0;
		pp_printf("Empty init script.. \n");
	}
	for (i = 1; i <= used; i++) {

		if (w1_read_eeprom_bus(&wrpc_w1_bus,
				       EE_BASE_INIT + (EE_BASE_CAL * i), buf,
				       W1_BUF) < 0)
			return EE_RET_1WERR;

		pp_printf("Command %d ", i);
		for (j = 0; j < strlen((char *)buf); ++j)
			mprintf("%c", buf[j]);
	}
	return 0;
}

/* A shell command, for testing write*/
int w1_eeprom_init_readcmd(uint8_t *buf, uint8_t next)
{
	static uint16_t read_cmd;
	static uint16_t num_cmd;
	uint8_t tmp_buf[W1_BUF];

	if (next == 0) {
		if (w1_read_eeprom_bus
		    (&wrpc_w1_bus, EE_BASE_INIT, tmp_buf, W1_BUF) < 0)
			return EE_RET_1WERR;
		num_cmd = (uint16_t) tmp_buf[0];
		if (num_cmd == 0xff)
			num_cmd = 0;
		read_cmd = 0;
	}
	pp_printf("Init script with %d commands\n", num_cmd);
	if (num_cmd == 0 || (num_cmd == read_cmd))
		return 0;

	read_cmd++;

	if (w1_read_eeprom_bus
	    (&wrpc_w1_bus, EE_BASE_INIT + (EE_BASE_CAL * read_cmd), buf,
	     W1_BUF) < 0)
		return EE_RET_1WERR;

	return (int32_t) strlen((char *)buf);
}

/* A shell command, for testing write*/
static int cmd_w1_w(const char *args[])
{
	int page;
	int max_blen = 32;
	int blen;
	char pn[32] = "\0";

	if (args[0] == '\0' || args[1] == '\0') {
		pp_printf("This commands needs arguments, "
			  "w1w PAGE_EEPROM STRING\n");
		return 0;
	}

	page = atoi(args[0]);
	blen = strlen(args[1]);

	if (blen > max_blen)
		blen = max_blen;
	memcpy(pn, args[1], blen);

	if (!w1_write_eeprom_bus
	    (&wrpc_w1_bus, page * 0x20, (uint8_t *) pn, blen))
		pp_printf("Write success\n");

	return 0;
}

DEFINE_WRC_COMMAND(w1w) = {
	.name = "w1w",
	.exec = cmd_w1_w,
};

/* A shell command, for testing read*/
static int cmd_w1_r(const char *args[])
{
	int page;
	int i = 0;
	int blen;
	uint8_t pn[32] = "\0";

	if (args[0] == '\0') {
		pp_printf("This commands needs arguments, w1r PAGE_EEPROM\n");
		return 0;
	}

	page = atoi(args[0]);
	w1_read_eeprom_bus(&wrpc_w1_bus, page * 0x20, pn, 32);

	blen = strlen((char *)pn);

	for (i = 0; i < blen; i++)
		pp_printf("%c", pn[i]);
	pp_printf("\n");

	return 0;
}

DEFINE_WRC_COMMAND(w1r) = {
	.name = "w1r",
	.exec = cmd_w1_r,
};

/* A shell command, for testing write/read*/
static int cmd_w1_test(const char *args[])
{
	int page;
	int errors = 0;
	int blen;
	int i = 0;
	char pn[32] = "testing";
	uint8_t *b = (void *)pn;

	blen = strlen(pn);
	for (page = 0; page < 80; page++) {
		if (!w1_write_eeprom_bus(&wrpc_w1_bus, page * 0x20, b, blen)) {
			pp_printf("Page %i: success\n", page);
		} else {
			pp_printf("Page %i: error\n", page);
			errors++;
		}
	}
	pp_printf("Write Errors: %d \n", errors);

	usleep(1000 * 1000);

	for (page = 0; page < 80; page++) {
		w1_read_eeprom_bus(&wrpc_w1_bus, page * 0x20, b, 32);

		blen = strlen(pn);

		pp_printf("Page %i: ", page);
		for (i = 0; i < blen; i++)
			pp_printf("%c", pn[i]);
		pp_printf("\n");
	}
	return 0;
}

DEFINE_WRC_COMMAND(w1test) = {
	.name = "w1test",
	.exec = cmd_w1_test,
};
