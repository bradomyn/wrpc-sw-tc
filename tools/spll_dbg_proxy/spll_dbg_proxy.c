/* SoftPLL debug proxy

	 Reads out the debug FIFO datastream from the SoftPLL and proxies it
	 via TCP connection to the application running on an outside host, where
	 the can be plotted, analyzed, etc.
	 
	 The debug stream contains run-time signals coming in/out the SoftPLL - for example,
	 the phase/frequency errors on each channel, DAC drive values, phase tags. 

	 Todo: poll the hardware FIFO through a driver with interrupt support
*/
	 
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "softpll_regs.h"
#include "speclib.h"
#include "sdb.h"

/* TCP Port to listen on */
#define MY_PORT 12345

/* Size of the software FIFO ring buffer */
#define RING_BUFFER_ENTRIES 1048576
#define ENTRIES_PER_PACKET 128

#define SPEC_SDB_ADDR 0

__attribute__((packed)) struct fifo_entry {
	uint32_t value;
	uint16_t seq_id;
};

static uint32_t spll_base ;
static void *spec_card;

static uint32_t spll_readl(uint32_t addr)
{
	return spec_readl(spec_card, spll_base + addr);
}

static void spll_writel(uint32_t data, uint32_t addr)
{
 	spec_writel(spec_card, data, addr);
}

void spec_init()
{
	if(getuid())
	{
	 	printf("I must be run as root. Sorry...\n");
	 	exit(-1);
	}

 	spec_card = spec_open(-1, -1);
	if(!spec_card)
	{
	 	printf("Failed to find any SPECs in your system...\n");
	 	exit(-1);
	}

	spll_base = sdb_find_device(spec_card, 0, 0x65158dc0);
}

/* 

Simple ring buffer implementation. WARNING: NOT thread-safe 

*/
struct ring_buffer {
	void *base;
	int entry_size, num_entries;
	int wr_ptr, rd_ptr, count;
};


int rbuf_init(struct ring_buffer *rbuf, int num_entries, int entry_size)
{
	rbuf->base = malloc(num_entries * entry_size);
	if(!rbuf->base)
		return -1;
	
	rbuf->entry_size = entry_size;
	rbuf->num_entries = num_entries;
	rbuf->wr_ptr = 0;
	rbuf->rd_ptr = 0;
	rbuf->count = 0;
	return 0;	
}

void rbuf_push(struct ring_buffer *rbuf, void *what)
{
	if(rbuf->count >= rbuf->num_entries-1) /* buffer full */
		return;
	
	rbuf->count++;
	memcpy(rbuf->base + rbuf->wr_ptr*rbuf->entry_size, what, rbuf->entry_size);
	rbuf->wr_ptr++;
	if(rbuf->wr_ptr == rbuf->num_entries)
		rbuf->wr_ptr = 0;
}

int rbuf_pop(struct ring_buffer *rbuf, void *dst)
{
	if(!rbuf->count) /* buffer empty */
		return 0;
	
	rbuf->count--;
	memcpy(dst, rbuf->base + rbuf->rd_ptr*rbuf->entry_size, rbuf->entry_size);
	rbuf->rd_ptr++;

	if(rbuf->rd_ptr == rbuf->num_entries)
		rbuf->rd_ptr = 0;
	return 1;
}

void rbuf_release(struct ring_buffer *rbuf)
{
	free(rbuf->base);
}

struct ring_buffer spll_trace;
static struct SPLL_WB *_spll_regs = (struct SPLL_WB*) 0;

#define REG(x) ((uint32_t)(&_spll_regs->x))

void poll_spll_fifo()
{
	
	while(1) {

/* Move the following lines (and the ring buffering code) to the driver. 

	for the SPLL: IRQ = 3  (asserted when FIFO != empty)
					base : check DFR_HOST register in softpll_regs.h
	
	device: /dev/spfifoX
	parameters: base_addr, num_regs (r0, r1), irq
	ioctls: 
	
*/

		uint32_t csr =	spll_readl(REG(DFR_HOST_CSR));
		struct fifo_entry ent;

//		fprintf(stderr,"CSR %x\n", csr);
		
		if(csr & SPLL_DFR_HOST_CSR_EMPTY) break;

		ent.value = spll_readl(REG(DFR_HOST_R0));
		ent.seq_id = spll_readl(REG(DFR_HOST_R1)) & 0xffff;


//		fprintf(stderr, "v: %x\n", ent.value);

		rbuf_push(&spll_trace, (void *)&ent);
	}
}

static int proxy_done = 0;

void sighandler(int sig)
{
	if(sig == SIGPIPE)
	{
		fprintf(stderr,"Connection broken. Killing proxy\n");
		proxy_done = 1;	
	}
}


void proxy_stuff(int fd)
{
	if(	rbuf_init(&spll_trace, RING_BUFFER_ENTRIES, sizeof(struct fifo_entry)) < 0)
	{
		perror("rbuf_init()");
		return ;
	}


	fprintf(stderr,"Connection accepted [record size %d].\n", sizeof(struct fifo_entry));
	proxy_done = 0;
	signal(SIGPIPE, sighandler);

	for(;;)
	{
		poll_spll_fifo();	
		
		while(spll_trace.count > ENTRIES_PER_PACKET)
		{
			struct fifo_entry tx_buf[ENTRIES_PER_PACKET];
			int i;
			
			for(i=0;i<ENTRIES_PER_PACKET;i++)
				rbuf_pop(&spll_trace, &tx_buf[i]); /* fixme: make endian-independent */

			if(proxy_done)
			{
				rbuf_release(&spll_trace);
				return;
			}

			if(send(fd, tx_buf, sizeof(tx_buf), 0) <= 0)
			{
				fprintf(stderr,"Connection closed.\n");
				rbuf_release(&spll_trace);
				return;
			}			
		}
//		fprintf(stderr,"Count :%d\n", spll_trace.count);
		usleep(10000);
	}
	
}

extern void shw_fpga_mmap_init();

int main(int argc, char *argv[])
{
		int sock_fd;
		struct sockaddr_in sin;

		spec_init();


		if((sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
		{
			perror("socket()");
			return -1;
		}

		int yes = 1;
		if (setsockopt(sock_fd,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes)) < 0) {
			perror("setsockopt()");
			return -1;
		}
		
    sin.sin_family = AF_INET;                /* Internet address family */
    sin.sin_addr.s_addr = htonl(INADDR_ANY); /* Any incoming interface */
    sin.sin_port = htons(MY_PORT); 	

    if (bind(sock_fd, (struct sockaddr *) &sin, sizeof(sin)) < 0)
    {
    	perror("bind()");
    	return -1;
    }

    if (listen(sock_fd, 1) < 0)
    {
    	perror("listen()");
			return -1;
		}

		
		for(;;)
		{
			struct sockaddr_in client_addr;
			socklen_t client_len = sizeof(client_addr);
			int client_fd;
		
			if((client_fd = accept(sock_fd, (struct sockaddr *) &client_addr, &client_len)) > 0)
				proxy_stuff(client_fd);
				
		}    	

		return 0;
}
