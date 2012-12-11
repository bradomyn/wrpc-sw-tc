/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 GSI (www.gsi.de)
 * Author: Wesley W. Terpstra <w.terpstra@gsi.de>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

#ifndef __SDB_H
#define __SDB_H

#include <stdint.h>

uint32_t sdb_find_device(void *card, uint32_t sdb_address, unsigned int devid);
void sdb_print_devices(void *card, uint32_t sdb_address);

#endif
