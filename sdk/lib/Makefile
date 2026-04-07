# Build libfh_mpi.so — clean-room ioctl wrapper for FH8852V201
#
# Requires ARM cross-compiler. Options:
#   1. arm-linux-gnueabihf-gcc (Debian/Ubuntu: apt install gcc-arm-linux-gnueabihf)
#   2. arm-fullhanv3-linux-uclibcgnueabi-gcc (Fullhan SDK toolchain)
#   3. Buildroot toolchain from OpenIPC

CC ?= arm-linux-gnueabihf-gcc
CFLAGS = -Wall -O2 -fPIC -march=armv7-a -mfloat-abi=hard -mfpu=neon-vfpv4
LDFLAGS = -shared

TARGET = libfh_mpi.so
SRCS = fh_mpi.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c fh_mpi.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)

# Install to camera via TFTP
install: $(TARGET)
	@echo "Upload to camera:"
	@echo "  scp $(TARGET) pi@aircast.local:/srv/tftp/"
	@echo "  Then on camera: tftp -l /tmp/$(TARGET) -r $(TARGET) -g 192.168.8.100"

.PHONY: all clean install
