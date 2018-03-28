vCFLAGS = -g
CFLAGS += -D_GNU_SOURCE -std=gnu99
CFLAGS += -g3 -Wall -Wextra -Wcast-qual -Wcast-align
CFLAGS += -Winline -Wfloat-equal -Wnested-externs
CFLAGS += -pthread -pedantic

CC = gcc
EXECS = server client
.PHONY: all clean

all: $(EXECS)

server:  db.c comm.c server.c
	$(CC) $^ $(CFLAGS) -o $@

client: client.c
	$(CC) $< $(CFLAGS) -o $@

clean:
	rm -f $(EXECS)
