CFLAGS += -O3 -mavx -msse4.2

.PHONY: all
all: test

test: test.o ec-method.o timer.o

test.o: test.c timer.h ec-method.h

ec-method.o: ec-method.c ec-method.h

timer.o: timer.c timer.h

.PHONY: clean
clean:
	$(RM) test test.o ec-method.o timer.o
