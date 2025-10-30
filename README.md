# Character Device Driver: Interrupt vs Kernel-Bypass Benchmark

A performance comparison between traditional interrupt-based I/O and kernel-bypass (mmap) approaches for Linux character device drivers.

## Overview

This project implements and benchmarks two approaches to kernel-user space data transfer:

1. **Interrupt-based Driver** - Uses `copy_to_user()` / `copy_from_user()` (traditional syscalls)
2. **Kernel-Bypass Driver** - Uses `mmap()` for direct memory access (zero-copy)

## Repository Structure

```
.
├── mydriver_interrupt.c    # Traditional interrupt-based driver
├── mydriver_bypass.c       # Kernel-bypass driver with mmap support
├── benchmark.c             # Performance benchmark program
├── Makefile               # Kernel module build
├── Makefile.benchmark     # Benchmark program build
├── run_benchmark.sh       # Automated test script
└── README.md
```

## Building

### Build Kernel Modules
```bash
make
```

This will build both `mydriver_interrupt.ko` and `mydriver_bypass.ko`.

### Build Benchmark Program
```bash
make -f Makefile.benchmark
```

## Quick Start

### Automated (Recommended)
```bash
sudo ./run_benchmark.sh
```

This will build, load, test, and cleanup automatically.

### Manual Setup

1. Build modules:
```bash
make
```

2. Build benchmark:
```bash
make -f Makefile.benchmark
```

3. Load drivers:
```bash
sudo insmod mydriver_interrupt.ko
sudo insmod mydriver_bypass.ko
```

4. Create device nodes:
```bash
# Get major numbers from dmesg
MAJOR_INT=$(dmesg | grep mydevice_interrupt | grep major | tail -1 | awk '{print $NF}')
MAJOR_BYP=$(dmesg | grep mydevice_bypass | grep major | tail -1 | awk '{print $NF}')

sudo mknod /dev/mydevice_interrupt c $MAJOR_INT 0
sudo mknod /dev/mydevice_bypass c $MAJOR_BYP 0
sudo chmod 666 /dev/mydevice_*
```

5. Run benchmark:
```bash
./benchmark
```

6. Cleanup:
```bash
sudo rmmod mydriver_interrupt mydriver_bypass
sudo rm /dev/mydevice_*
```

## Benchmark Details

The benchmark performs:
- **10,000 iterations** of 256-byte transfers
- Both read and write operations
- Direct comparison of syscall vs mmap performance

### Test Scenarios

1. **Interrupt Driver**: Traditional `read()`/`write()` syscalls with `copy_to_user()`/`copy_from_user()`
2. **Bypass Driver**: Persistent mmap (map once, use many times) with direct memory access

## Expected Results

**Typical Performance:**
- **Interrupt Driver**: ~3-4 μs per operation, ~70 MB/s
- **Kernel-Bypass (mmap)**: ~0.003 μs per operation, ~80,000 MB/s

**Speedup**: ~1000x faster for persistent mmap vs traditional syscalls!

## Why Is mmap So Much Faster?

1. **No syscall overhead** - Direct memory access from user space
2. **No data copying** - Eliminates `copy_to_user()`/`copy_from_user()`
3. **No context switches** - Stays in user mode
4. **Persistent mapping** - Setup cost amortized over many operations

## Architecture

### Interrupt-Based Driver
- Buffer allocation: `kmalloc()` (1024 bytes)
- Data transfer: `copy_to_user()` / `copy_from_user()`
- Standard character device operations

### Kernel-Bypass Driver
- Buffer allocation: `vmalloc_user()` (PAGE_SIZE = 4096 bytes)
- Data transfer: Direct memory access via `mmap()`
- Also supports traditional `read()`/`write()` for compatibility
- Includes `poll()` support for event notification

## Use Cases

**Use Interrupt-Based I/O when:**
- One-shot operations
- Infrequent transfers
- Simplicity is more important than performance
- Small data transfers with long intervals

**Use Kernel-Bypass (mmap) when:**
- High-frequency operations (>1000 ops/sec)
- Low-latency requirements (real-time, trading, etc.)
- Persistent connections with many small transfers
- Zero-copy semantics needed (DMA-like access)

## Cleaning Up

```bash
make clean
make -f Makefile.benchmark clean
```

## Requirements

- Linux kernel headers (`linux-headers-$(uname -r)`)
- GCC compiler
- Root access (for loading kernel modules)
