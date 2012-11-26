/* CRAP CODE. DO NOT MERGE. DO NOT USE. */

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

#include "simple-eb.h"

static char *load_binary_file(const char *filename, size_t *size)
{
	int i;
	struct stat stbuf;
	char *buf;
	FILE *f;

	f = fopen(filename, "r");
	if (!f) {
		fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		return NULL;
	}
	if (fstat(fileno(f), &stbuf) < 0) {
		fprintf(stderr, "%s: %s\n", filename, strerror(errno));
		fclose(f);
		return NULL;
	}

	if (!S_ISREG(stbuf.st_mode)) {
		fprintf(stderr, "%s: not a regular file\n", filename);
		fclose(f);
		return NULL;
	}

	buf = malloc(stbuf.st_size);
	if (!buf) {
		fprintf(stderr, "loading %s: %s\n", filename, strerror(errno));
		fclose(f);
		return NULL;
	}

	i = fread(buf, 1, stbuf.st_size, f);
	fclose(f);
	if (i < 0) {
		fprintf(stderr, "reading %s: %s\n", filename, strerror(errno));
		free(buf);
		return NULL;
	}
	if (i != stbuf.st_size) {
		fprintf(stderr, "%s: short read\n", filename);
		free(buf);
		return NULL;
	}

	*size = stbuf.st_size;
	return buf;
}

int eb_load_lm32(eb_device_t dev, const char *filename, uint32_t base_addr)
{
	char *buf;
	uint32_t *ibuf;
	size_t size;
	int i;

	buf = load_binary_file(filename, &size);
	if(!buf)
		return -1;

	/* Phew... we are there, finally */
	ebs_write(dev, base_addr + 0x20400, 0x1deadbee);
	while ( ! (ebs_read(dev, base_addr + 0x20400) & (1<<28)) );

	ibuf = (uint32_t *) buf;
	for (i = 0; i < (size + 3) / 4; i++)
		ebs_write(dev, base_addr + i*4, htonl(ibuf[i]));

	for (i = 0; i < (size + 3) / 4; i++) {
		uint32_t r = ebs_read(dev, base_addr + i * 4);
		if (r != htonl(ibuf[i]))
		{
			fprintf(stderr, "programming error at %x "
				"(expected %08x, found %08x)\n", i*4,
				htonl(ibuf[i]), r);
			return -1;
		}
	}

	ebs_write(dev, base_addr + 0x20400,  0x0deadbee);
	return 0;
}


main(int argc, char *argv[])
{
	if(argc < 3) { 
		printf("usage: %s address file.bin\n", argv[0]);
		return 0;
	}

	eb_device_t dev;
		
	ebs_init();
	ebs_open(&dev, argv[1]);
	eb_load_lm32(dev, argv[2], 0xc0000);
	ebs_close(dev);
	ebs_shutdown();
	return 0;
}