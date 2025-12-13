CC = gcc
CFLAGS = -Wall -Wextra -O2 -g
LDFLAGS =
SRCDIR = src
OBJS = main.o netlink.o parser.o metrics.o alert.o cli.o logger.o

.PHONY: all clean

all: nlagent

nlagent: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

%.o: $(SRCDIR)/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f *.o nlagent
