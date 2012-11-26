OBJS = simple-eb.o lm32-etherboot.o
CFLAGS = -g -Ietherbone -I.
LDFLAGS = -Letherbone -letherbone

all:	$(OBJS)
			gcc -o lm32-etherboot $(OBJS) $(LDFLAGS)