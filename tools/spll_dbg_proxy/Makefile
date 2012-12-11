
CC = $(CROSS_COMPILE)gcc

OBJS = spec-sw/tools/speclib.o  spll_dbg_proxy.o sdb.o

BINARY = spll_dbg_proxy

# We must include stuff from wr_ipc, which is installed.
# If this is build under build scripts, it's $WRS_OUTPUT_DIR/images/wr

CFLAGS = -g -Wall -DDEBUG -Ispec-sw/kernel -Ispec-sw/tools -ffunction-sections -fdata-sections \
	-I. 

LDFLAGS = -lm -ldl -Wl,--gc-sections

all: $(BINARY)

$(BINARY): $(OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

scp: all
		scp $(BINARY) root@pts-test03:/tftpboot/192.168.1.2/root

clean:
		rm -f $(BINARY) $(OBJS)
