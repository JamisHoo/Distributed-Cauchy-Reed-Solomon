vpath %.c ec
vpath %.h ec

CFLAGS += -O2 -g
LDLIBS += -pthread -libverbs -lrdmacm

.PHONY:all
all: server client

server: server.o

client: client.o ec-method.o ec-gf.o

server.o: server.c nodes.h

client.o: client.c ec-method.h nodes.h

ec-method.o: ec-method.c ec-method.h ec-gf.h

ec-gf.o: ec-gf.c ec-gf.h

check.o: check.c nodes.h ec-method.h

check: check.o ec-method.o ec-gf.o

.PHONY: clean
clean:
	$(RM) server client server.o client.o ec-method.o ec-gf.o check.o check
