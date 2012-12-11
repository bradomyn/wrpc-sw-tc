#ifndef __I2C_H
#define __I2C_H

#define MAX_I2C_BUSSES 3

#define WRPC_FMC_I2C  0
#define WRPC_SFP_I2C  1
#define WRPC_AUX_I2C  2

/* definition of bit-banged I2C interface. scl() and sda() functions:
	- return the current state of the SCL and SDA lines.
	- set the new state of the lines: 0 = forced 0, 1 = pullup
*/

struct i2c_bitbang_interface {
	int (*scl)(int state);
	int (*sda)(int state);
};

void mi2c_register_interface(uint8_t i2cif, struct i2c_bitbang_interface *iface);
uint8_t mi2c_devprobe(uint8_t i2cif, uint8_t i2c_addr);
void mi2c_init(uint8_t i2cif);
void mi2c_start(uint8_t i2cif);
void mi2c_repeat_start(uint8_t i2cif);
void mi2c_stop(uint8_t i2cif);
void mi2c_get_byte(uint8_t i2cif, unsigned char *data, uint8_t last);
unsigned char mi2c_put_byte(uint8_t i2cif, unsigned char data);
void mi2c_scan(uint8_t i2cif);

#endif
