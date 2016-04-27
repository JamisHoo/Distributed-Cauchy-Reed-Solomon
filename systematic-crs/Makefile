CFLAGS += -Wextra -Wno-sign-compare -O3 -mavx -msse4.2

LDFLAGS += -pthread

.PHONY: all
all: test

test: test.o ec-gf.o ec-method.o timer.o thpool.o

test.o: test.c timer.h ec-method.h ec-gf.h

ec-gf.o: ec-gf.h ec-gf.c

ec-method.o: ec-method.c ec-method.h ec-gf.h

timer.o: timer.c timer.h

thpool.o: thpool.c thpool.h

.PHONY: clean
clean:
	$(RM) test test.o ec-gf.o ec-method.o timer.o thpool.o
