LDFLAGS=-ldl -pthread -w 
CC = gcc
CROSSCC = arm-linux-gnueabihf-gcc
LIBS += lgpiod
all: cross

cross:
	test -d dist || mkdir dist
	$(CROSSCC) gpio.c $(LDFLAGS) -o dist/gpio

test:
	mkdir test
	$(CC) gpio.c $(LDFLAGS) -o test/gpio
	$(CC) -shared ./mocks/libdevice_kit.c -o test/libdevice_kit.so

clean:
	$(RM) -rf test dist