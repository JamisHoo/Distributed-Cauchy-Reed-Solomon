vpath %.c ec
vpath %.h ec

CFLAGS += -O2 -g
LDLIBS += -pthread -libverbs -lrdmacm

.PHONY:all
all: encode_server encode_client

encode_server: encode_server.o

encode_client: encode_client.o ec-method.o ec-gf.o

encode_server.o: encode_server.c nodes.h

enconde_client.o: encode_client.c ec-method.h nodes.h

ec-method.o: ec-method.c ec-method.h ec-gf.h

ec-gf.o: ec-gf.c ec-gf.h

check.o: check.c nodes.h ec-method.h

check: check.o ec-method.o ec-gf.o

.PHONY: clean
clean:
	$(RM) encode_server encode_client encode_server.o encode_client.o ec-method.o ec-gf.o check.o check
