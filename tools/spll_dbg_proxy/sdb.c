/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 GSI (www.gsi.de)
 * Author: Wesley W. Terpstra <w.terpstra@gsi.de>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */
#include <string.h>
#include <stdio.h>

#include "speclib.h"

#define SDB_INTERCONNET 0x00
#define SDB_DEVICE      0x01
#define SDB_BRIDGE      0x02
#define SDB_EMPTY       0xFF

typedef struct pair64 {
	uint32_t high;
	uint32_t low;
} pair64_t;

struct sdb_empty {
	int8_t reserved[63];
	uint8_t record_type;
};

struct sdb_product {
	pair64_t vendor_id;
	uint32_t device_id;
	uint32_t version;
	uint32_t date;
	int8_t name[19];
	uint8_t record_type;
};

struct sdb_component {
	pair64_t addr_first;
	pair64_t addr_last;
	struct sdb_product product;
};

struct sdb_device {
	uint16_t abi_class;
	uint8_t abi_ver_major;
	uint8_t abi_ver_minor;
	uint32_t bus_specific;
	struct sdb_component sdb_component;
};

struct sdb_bridge {
	pair64_t sdb_child;
	struct sdb_component sdb_component;
};

struct sdb_interconnect {
	uint32_t sdb_magic;
	uint16_t sdb_records;
	uint8_t sdb_version;
	uint8_t sdb_bus_type;
	struct sdb_component sdb_component;
};

typedef union sdb_record {
	struct sdb_empty empty;
	struct sdb_device device;
	struct sdb_bridge bridge;
	struct sdb_interconnect interconnect;
} sdb_record_t;

#if 0
static unsigned char *find_device_deep(void *card, unsigned int base, unsigned int sdb,
				       unsigned int devid)
{
	sdb_record_t *record = (sdb_record_t *) sdb;
	int records = record->interconnect.sdb_records;
	int i;

	for (i = 0; i < records; ++i, ++record) {
		if (record->empty.record_type == SDB_BRIDGE) {
			unsigned char *out =
			    find_device_deep(base +
					     record->bridge.sdb_component.
					     addr_first.low,
					     record->bridge.sdb_child.low,
					     devid);
			if (out)
				return out;
		}
		if (record->empty.record_type == SDB_DEVICE &&
		    record->device.sdb_component.product.device_id == devid) {
			break;
		}
	}

	if (i == records)
		return 0;
	return (unsigned char *)(base +
				 record->device.sdb_component.addr_first.low);
}
#endif

static void spec_read_buffer(void *card, void *buf, uint32_t base, size_t size)
{
	int i;
	
	for(i=0; i<(size + 3) / 4; i++)
		((uint32_t*)buf)[i] = spec_readl(card, base + i * 4);
}

static void print_devices_deep(void *card, unsigned int base, unsigned int sdb)
{
	sdb_record_t record ;
	spec_read_buffer(card, &record, sdb, sizeof(sdb_record_t)); 

	int records = record.interconnect.sdb_records;
	int i;
	char buf[20];

	

	for (i = 0; i < records; ++i) {
		spec_read_buffer(card, &record, sdb + i * sizeof(sdb_record_t), sizeof(sdb_record_t)); 

		if (record.empty.record_type == SDB_BRIDGE)
			print_devices_deep(card, 
						base + record.bridge.sdb_component.
					   addr_first.low,
					   record.bridge.sdb_child.low);

		if (record.empty.record_type != SDB_DEVICE)
			continue;

		memcpy(buf, record.device.sdb_component.product.name, 19);
		buf[19] = 0;
		printf("%8x:%8x 0x%8x %s\n",
			record.device.sdb_component.product.vendor_id.low,
			record.device.sdb_component.product.device_id,
			base + record.device.sdb_component.addr_first.low,
			buf);
	}
}

uint32_t sdb_find_device(void *card, uint32_t sdb_address, unsigned int devid)
{
	printf("Fixme: implement %s\n", __FUNCTION__);
	return 0x20200;
//	return find_device_deep(0, SDB_ADDRESS, devid);
} 

void sdb_print_devices(void *card, uint32_t sdb_address)
{
	printf("SDB memory map:\n");
	print_devices_deep(card, 0, sdb_address);
	printf("---\n");
}

