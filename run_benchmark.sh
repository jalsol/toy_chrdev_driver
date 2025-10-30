#!/bin/bash

# Script to setup, load, and benchmark the character device drivers

set -e

echo "Building kernel modules..."
make clean
make

echo ""
echo "Building benchmark program..."
make -f Makefile.benchmark clean
make -f Makefile.benchmark

echo ""
echo "Removing old modules (if loaded)..."
sudo rmmod mydriver_interrupt 2>/dev/null || true
sudo rmmod mydriver_bypass 2>/dev/null || true

echo ""
echo "Loading interrupt-based driver..."
sudo insmod mydriver_interrupt.ko
INTERRUPT_MAJOR=$(dmesg | grep "mydevice_interrupt.*Registered with major number" | tail -1 | awk '{print $NF}')
echo "Interrupt driver major number: $INTERRUPT_MAJOR"

echo ""
echo "Loading kernel-bypass driver..."
sudo insmod mydriver_bypass.ko
BYPASS_MAJOR=$(dmesg | grep "mydevice_bypass.*Registered with major number" | tail -1 | awk '{print $NF}')
echo "Bypass driver major number: $BYPASS_MAJOR"

echo ""
echo "Creating device nodes..."
sudo rm -f /dev/mydevice_interrupt /dev/mydevice_bypass
sudo mknod /dev/mydevice_interrupt c $INTERRUPT_MAJOR 0
sudo mknod /dev/mydevice_bypass c $BYPASS_MAJOR 0
sudo chmod 666 /dev/mydevice_interrupt
sudo chmod 666 /dev/mydevice_bypass

echo ""
echo "Device nodes created:"
ls -l /dev/mydevice_*

echo ""
echo "Running benchmark..."
echo ""
./benchmark

echo ""
echo "Cleaning up..."
sudo rmmod mydriver_interrupt
sudo rmmod mydriver_bypass
sudo rm -f /dev/mydevice_interrupt /dev/mydevice_bypass

echo ""
echo "Done!"
