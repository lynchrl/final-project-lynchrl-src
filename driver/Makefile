# Makefile for bme280 driver.
# Define the object file to be built as a module
obj-m += bme280.o

# Path to the kernel build directory
KDIR := /lib/modules/$(shell uname -r)/build
# Current directory
PWD := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
