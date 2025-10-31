# Quick Start Guide - Producer-Consumer Queue

## TL;DR - Run the Benchmark

```bash
# Build and run in one command:
sudo ./run_prodcons_benchmark.sh
```

That's it! The script will:
1. Build the kernel module
2. Build the benchmark program
3. Load the module
4. Create the device node
5. Run the benchmark
6. Show results and kernel stats
7. Clean up automatically

## What You'll See

### Test 1: Syscall Interface
- User produces data via `write()` 
- Kernel consumes data concurrently
- Performance: ~600 ns per operation

### Test 2: mmap Zero-Copy
- User and kernel both produce AND consume
- Direct shared memory access
- Performance: ~11 ns per operation (55x faster!)

### Sample Output
```
========================================
Producer-Consumer Queue Benchmark
========================================
Queue Size: 4032 items
Iterations: 100000

=== Test 1: Syscall Producer-Consumer ===
Write: 1000 operations
  Total: 607 μs | Avg: 607.51 ns/op
Read : 1000 operations
  Total: 549 μs | Avg: 549.73 ns/op

=== Test 2: mmap Producer-Consumer ===
User produced 831 items via mmap (zero-copy)
Produce: 831 operations
  Total: 9 μs | Avg: 11.01 ns/op

Final state:
  Kernel produced: 34,015,000
  Kernel consumed: 27,739,134
  User produced: 6,696
  User consumed: 7,681,467
```

## Understanding the Results

### Kernel Threads are Fast!
The kernel producer creates ~34 million items while the test runs.
The kernel consumer processes ~27 million items.
This happens in just a few seconds!

### mmap is Much Faster
- Syscalls: ~600 ns per operation
- mmap: ~11 ns per operation
- **Speedup: 55x faster!**

### Why mmap Wins
1. **No system calls** - Direct memory access
2. **No data copying** - Zero-copy semantics
3. **No context switches** - Stay in user mode
4. **Lock-free** - Atomic operations only

## Manual Testing

If you want to run steps manually:

```bash
# 1. Build
make
make -f Makefile.benchmark

# 2. Load module
sudo insmod mydriver_prodcons.ko

# 3. Create device (get major from dmesg)
MAJOR=$(dmesg | grep mydevice_bypass | grep major | tail -1 | awk '{print $NF}')
sudo mknod /dev/mydevice_bypass c $MAJOR 0
sudo chmod 666 /dev/mydevice_bypass

# 4. Run benchmark
./benchmark_prodcons

# 5. Check kernel logs
dmesg | grep mydevice_bypass | tail -20

# 6. Cleanup
sudo rmmod mydriver_prodcons
sudo rm /dev/mydevice_bypass
```

## Troubleshooting

### "Failed to open device"
- Make sure the module is loaded: `lsmod | grep mydriver`
- Check device exists: `ls -l /dev/mydevice_bypass`
- Check permissions: Should be `crw-rw-rw-`

### "Cannot insert module"
- Check if already loaded: `sudo rmmod mydriver_prodcons`
- Check kernel logs: `dmesg | tail -20`

### "Permission denied"
- Must run as root: `sudo ./run_prodcons_benchmark.sh`

## Monitoring in Real-Time

Open two terminals:

**Terminal 1:**
```bash
# Watch kernel logs
sudo dmesg -w | grep mydevice_bypass
```

**Terminal 2:**
```bash
# Run benchmark
sudo ./run_prodcons_benchmark.sh
```

You'll see the kernel threads producing and consuming data in real-time!

## Key Files

- `mydriver_prodcons.c` - Kernel driver implementation
- `benchmark_prodcons.cpp` - Benchmark program
- `run_prodcons_benchmark.sh` - Automated test script
- `PRODCONS_SUMMARY.md` - Detailed technical summary
- `ARCHITECTURE.txt` - Visual architecture diagrams
- `README.md` - Full documentation

## Next Steps

- Read `ARCHITECTURE.txt` for visual diagrams
- Read `PRODCONS_SUMMARY.md` for technical details
- Modify the benchmark to test different scenarios
- Experiment with queue sizes and iteration counts
- Add your own producers/consumers

## Happy Benchmarking! 🚀
