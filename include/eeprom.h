#ifndef __EEPROM_H
#define __EEPROM_H

#define SFP_SECTION_PATTERN 0xdeadbeef
#define SFPS_MAX 4
#define SFP_PN_LEN 16
/*I2C bus*/
#ifdef CONFIG_EEPROM_I2C
#define EE_BASE_CAL 4*1024
#define EE_BASE_SFP 4*1024+4
#define EE_BASE_INIT 4*1024+SFPS_MAX*29
#endif
/*One Wire bus*/
#ifdef CONFIG_EEPROM_W1
#define EE_BASE_CAL  0x20
#define EE_BASE_MAC  0x0
#define EE_BASE_SFP  0x0200
#define EE_BASE_INIT 0x0400
#endif
#define W1_BUF       32

#define EE_RET_I2CERR -1
#define EE_RET_1WERR  -1
#define EE_RET_DBFULL -2
#define EE_RET_CORRPT -3
#define EE_RET_POSERR -4

extern int32_t sfp_alpha;
extern int32_t sfp_deltaTx;
extern int32_t sfp_deltaRx;
extern uint32_t cal_phase_transition;
extern uint8_t has_eeprom;
extern uint8_t has_w1_eeprom;

struct s_sfpinfo {
	char pn[SFP_PN_LEN];
	int32_t alpha;
	int32_t dTx;
	int32_t dRx;
	uint8_t chksum;
} __attribute__ ((__packed__));

uint8_t eeprom_present(uint8_t i2cif, uint8_t i2c_addr);
int eeprom_read(uint8_t i2cif, uint8_t i2c_addr, uint32_t offset, uint8_t * buf,
		size_t size);
int eeprom_write(uint8_t i2cif, uint8_t i2c_addr, uint32_t offset,
		 uint8_t * buf, size_t size);

int32_t eeprom_sfpdb_erase(uint8_t i2cif, uint8_t i2c_addr);
int32_t eeprom_sfp_section(uint8_t i2cif, uint8_t i2c_addr, size_t size,
			   uint16_t * section_sz);
int8_t eeprom_match_sfp(uint8_t i2cif, uint8_t i2c_addr, struct s_sfpinfo *sfp);

int8_t eeprom_phtrans(uint8_t i2cif, uint8_t i2c_addr, uint32_t * val,
		      uint8_t write);

int8_t eeprom_init_erase(uint8_t i2cif, uint8_t i2c_addr);
int8_t eeprom_init_add(uint8_t i2cif, uint8_t i2c_addr, const char *args[]);
int32_t eeprom_init_show(uint8_t i2cif, uint8_t i2c_addr);
int8_t eeprom_init_readcmd(uint8_t i2cif, uint8_t i2c_addr, uint8_t * buf,
			   uint8_t bufsize, uint8_t next);

int32_t eeprom_get_sfp(uint8_t i2cif, uint8_t i2c_addr, struct s_sfpinfo *sfp,
		       uint8_t add, uint8_t pos);
int8_t eeprom_init_purge(uint8_t i2cif, uint8_t i2c_addr);

#endif
