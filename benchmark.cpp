#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>

using namespace std::chrono;

constexpr int NUM_ITERATIONS = 10'000;
constexpr int DATA_SIZE = 256;
constexpr int PAGE_SIZE = 4096;

template <typename Func>
auto time_benchmark(Func&& func) {
  auto start = high_resolution_clock::now();
  func();
  auto end = high_resolution_clock::now();
  return duration_cast<nanoseconds>(end - start);
}

void print_metrics(const char* op, nanoseconds duration) {
  auto ns_count = duration.count();
  auto us_count = duration_cast<microseconds>(duration).count();
  double avg_ns = static_cast<double>(ns_count) / NUM_ITERATIONS;
  
  // Correct throughput calculation
  double seconds = ns_count / 1'000'000'000.0;
  double total_mb = (static_cast<double>(NUM_ITERATIONS) * DATA_SIZE) / (1024.0 * 1024.0);
  double throughput_mbps = total_mb / seconds;

  std::cout << op << ": " << NUM_ITERATIONS << " ops × " << DATA_SIZE << " bytes\n";
  std::cout << "  Total: " << us_count << " μs | Avg: " << std::fixed
            << std::setprecision(0) << avg_ns << " ns/op | Throughput: "
            << std::setprecision(2) << throughput_mbps << " MB/s\n";
}

void benchmark_interrupt() {
  std::cout << "\n=== Interrupt-Based Driver (Traditional Syscalls) ===\n";

  char write_buf[DATA_SIZE];
  char read_buf[DATA_SIZE];

  for (int i = 0; i < DATA_SIZE; i++) {
    write_buf[i] = 'A' + (i % 26);
  }

  int fd = ::open("/dev/mydevice_interrupt", O_RDWR);
  if (fd < 0) {
    std::cerr << "Failed to open interrupt device\n";
    return;
  }

  // Separate write test
  auto write_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      ::write(fd, write_buf, DATA_SIZE);
    }
  });
  print_metrics("Write", write_duration);

  // Reset device - reopen to reset cursors
  ::close(fd);
  fd = ::open("/dev/mydevice_interrupt", O_RDWR);
  if (fd < 0) {
    std::cerr << "Failed to re-open interrupt device\n";
    return;
  }

  // Pre-write data for the read test
  for (int i = 0; i < NUM_ITERATIONS; i++) {
    ::write(fd, write_buf, DATA_SIZE);
  }

  // Reset read position
  ::lseek(fd, 0, SEEK_SET);

  // Separate read test
  auto read_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      ::read(fd, read_buf, DATA_SIZE);
    }
  });
  print_metrics("Read ", read_duration);

  ::close(fd);
}

void benchmark_bypass() {
  std::cout << "\n=== Kernel-Bypass Driver (Persistent mmap) ===\n";

  char write_buf[DATA_SIZE];
  char read_buf[DATA_SIZE];

  for (int i = 0; i < DATA_SIZE; i++) {
    write_buf[i] = 'A' + (i % 26);
  }

  int fd = ::open("/dev/mydevice_bypass", O_RDWR);
  if (fd < 0) {
    std::cerr << "Failed to open bypass device\n";
    return;
  }

  volatile char* mapped = static_cast<volatile char*>(
      ::mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0));
  if (mapped == MAP_FAILED) {
    std::cerr << "mmap failed\n";
    ::close(fd);
    return;
  }

  // Write test - byte-by-byte volatile access
  // Note: This is "pessimistic" (slower than optimized memcpy) but prevents
  // compiler from optimizing away the benchmark entirely
  auto write_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      for (int j = 0; j < DATA_SIZE; j++) {
        mapped[j] = write_buf[j];
      }
    }
  });
  print_metrics("Write", write_duration);

  // Read test - byte-by-byte volatile access
  auto read_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS; i++) {
      for (int j = 0; j < DATA_SIZE; j++) {
        read_buf[j] = mapped[j];
      }
    }
  });
  print_metrics("Read ", read_duration);

  ::munmap(const_cast<char*>(mapped), PAGE_SIZE);
  ::close(fd);
}

int main() {
  benchmark_interrupt();
  benchmark_bypass();
  return 0;
}
