#!/bin/bash

set -e

echo "=========================================="
echo "Producer-Consumer Benchmark Script"
echo "=========================================="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Check if running as root
if [ "$EUID" -ne 0 ]; then 
    echo -e "${RED}Please run as root (use sudo)${NC}"
    exit 1
fi

echo -e "${YELLOW}[1/6] Building kernel module...${NC}"
make

echo -e "${YELLOW}[2/6] Building benchmark program...${NC}"
make -f Makefile.benchmark

echo -e "${YELLOW}[3/6] Loading kernel module...${NC}"
# Unload if already loaded
if lsmod | grep -q "mydriver_prodcons"; then
    rmmod mydriver_prodcons
fi
insmod mydriver_prodcons.ko

# Get the major number from dmesg
sleep 1
MAJOR=$(dmesg | grep "mydevice_bypass.*Registered with major number" | tail -1 | awk '{print $NF}')
echo "Detected major number: $MAJOR"

echo -e "${YELLOW}[4/6] Creating device node...${NC}"
# Remove old device node if exists
if [ -e /dev/mydevice_bypass ]; then
    rm /dev/mydevice_bypass
fi
mknod /dev/mydevice_bypass c $MAJOR 0
chmod 666 /dev/mydevice_bypass
ls -l /dev/mydevice_bypass

echo -e "${YELLOW}[5/6] Running producer-consumer benchmark...${NC}"
echo ""
./benchmark_prodcons

echo ""
echo -e "${YELLOW}[6/6] Checking kernel logs...${NC}"
echo ""
dmesg | grep "mydevice_bypass" | tail -20

echo ""
echo -e "${YELLOW}Cleaning up...${NC}"
rm /dev/mydevice_bypass
rmmod mydriver_prodcons

echo ""
echo -e "${GREEN}=========================================="
echo "Benchmark Complete!"
echo "==========================================${NC}"
