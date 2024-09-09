CC ?= gcc
LDFLAGS ?= -lpthread -lrt

all: aesdsocket

aesdsocket: aesdsocket.c      
	$(CC) $(LDFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket *.o