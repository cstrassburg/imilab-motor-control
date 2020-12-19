LDFLAGS=-ldl -pthread -w 
CC = gcc
CROSSCC = arm-linux-gnueabihf-gcc

all: cross

cross:
	test -d dist || mkdir dist
	$(CROSSCC) motord.c $(LDFLAGS) -o dist/motord

test:
	mkdir test
	$(CC) motord.c $(LDFLAGS) -o test/motord
	$(CC) -shared ./mocks/libdevice_kit.c -o test/libdevice_kit.so

clean:
	$(RM) -rf test dist