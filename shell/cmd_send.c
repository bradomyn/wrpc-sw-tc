/*
 * This work is part of the White Rabbit project
 *
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Grzegorz Daniluk <grzegorz.daniluk@cern.ch>
 * Author: Tomasz Wlostowski <tomasz.wlostowski@cern.ch>
 *
 * Released according to the GNU GPL, version 2 or any later version.
 */

/* 	Command: cal_server
		Arguments: none

		Description: launches a calibration server, allowing the slave
		to measure master-slave clock offsets on demand. Used only for verification
		of the timestamp processing algorithms.
*/

#include <string.h>
#include <wrc.h>
#include "shell.h"
#include "eeprom.h"
#include "syscon.h"
#include "rxts_calibrator.h"
#include "ptpd_netif.h"

struct cal_packet_t {
	int seq;
	int is_followup;
	wr_timestamp_t ts;
};

static int cmd_cal_server(const char *args[])
{
	uint8_t mcast[] = {0x01, 0x1b, 0x19, 0 , 0, 0};
	wr_socket_t *sock;
	wr_sockaddr_t addr;

	addr.family = PTPD_SOCK_RAW_ETHERNET;
	addr.ethertype = 0x88f7;
	memcpy(addr.mac, &mcast, 6);
	sock = ptpd_netif_create_socket(PTPD_SOCK_RAW_ETHERNET, 0, &addr);
	
	printf("Calibration service running\n");	
	
	for(;;)
	{
		uint8_t buf[256];
		struct cal_packet_t *req = buf;
		wr_timestamp_t ts;

		while(1)
		{
			update_rx_queues();
			int n = ptpd_netif_recvfrom(sock, &addr, buf, 256, &ts);
			if(n>0) break;
		}

		addr.family = PTPD_SOCK_RAW_ETHERNET;
		addr.ethertype = 0x88f7;
		memcpy(addr.mac, &mcast, 6);
		
		req->is_followup = 0;
		ptpd_netif_sendto	(sock, &addr, req, sizeof(struct cal_packet_t), &ts);
		req->is_followup = 1;
		req->ts = ts;
		ptpd_netif_sendto	(sock, &addr, req, sizeof(struct cal_packet_t), NULL);
	}
	return 0;
}

DEFINE_WRC_COMMAND(cal_server) = {
	.name = "cal_server",
	.exec = cmd_cal_server,
};
