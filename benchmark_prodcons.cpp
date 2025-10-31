#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace std::chrono;

constexpr int NUM_ITERATIONS = 100'000;
constexpr int PAGE_SIZE = 4096;
constexpr int CONTROL_SIZE = 64;
constexpr int QUEUE_SIZE = PAGE_SIZE - CONTROL_SIZE;  // Must match kernel

// Shared control structure (must match kernel definition)
struct shared_control {
    std::atomic<int> write_idx;
    std::atomic<int> read_idx;
    std::atomic<int> kernel_produced;
    std::atomic<int> kernel_consumed;
    std::atomic<int> user_produced;
    std::atomic<int> user_consumed;
};

// Helper functions for queue operations
inline int queue_available_data(const shared_control* ctrl) {
    int write_idx = ctrl->write_idx.load(std::memory_order_acquire);
    int read_idx = ctrl->read_idx.load(std::memory_order_acquire);
    return (write_idx - read_idx + QUEUE_SIZE) % QUEUE_SIZE;
}

inline int queue_available_space(const shared_control* ctrl) {
    return QUEUE_SIZE - queue_available_data(ctrl) - 1;
}

inline bool queue_is_empty(const shared_control* ctrl) {
    return queue_available_data(ctrl) == 0;
}

inline bool queue_is_full(const shared_control* ctrl) {
    return queue_available_space(ctrl) == 0;
}

template <typename Func>
auto time_benchmark(Func&& func) {
  auto start = high_resolution_clock::now();
  func();
  auto end = high_resolution_clock::now();
  return duration_cast<nanoseconds>(end - start);
}

void print_metrics(const char* op, nanoseconds duration, int count) {
  auto ns_count = duration.count();
  auto us_count = duration_cast<microseconds>(duration).count();
  double avg_ns = static_cast<double>(ns_count) / count;
  double ops_per_sec = 1'000'000'000.0 / avg_ns;  // Convert ns to ops/sec
  
  std::cout << op << ": " << count << " operations (total: " << us_count << " μs)\n";
  std::cout << "  ⚡ Latency:    " << std::fixed << std::setprecision(2) 
            << avg_ns << " ns/op\n";
  std::cout << "  🚀 Throughput: " << std::fixed << std::setprecision(2) 
            << (ops_per_sec / 1'000'000.0) << " M ops/sec\n";
}

// Test 1: User produces via syscalls, kernel consumes (and also produces)
std::pair<double, double> test_syscall_produce_consume() {
  std::cout << "\n=== Test 1: Syscall Producer-Consumer (User produces, Kernel consumes) ===\n";

  int fd = ::open("/dev/mydevice_bypass", O_RDWR);
  if (fd < 0) {
    std::cerr << "Failed to open device\n";
    return {0.0, 0.0};
  }

  char write_buf[256];
  char read_buf[256];
  
  for (int i = 0; i < 256; i++) {
    write_buf[i] = 'Z' - (i % 26);  // Different from kernel's 'A'-'Z'
  }

  // Let kernel producer run for a bit first
  std::cout << "Waiting for kernel producer to generate data...\n";
  ::sleep(2);

  // User produces data via write()
  int total_produced = 0;
  auto produce_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS / 100; i++) {
      ssize_t written = ::write(fd, write_buf, 256);
      if (written > 0) {
        total_produced += written;
      }
    }
  });
  
  std::cout << "User produced " << total_produced << " bytes via write()\n";
  print_metrics("Write", produce_duration, NUM_ITERATIONS / 100);
  double write_latency = static_cast<double>(produce_duration.count()) / (NUM_ITERATIONS / 100);

  // User consumes data via read() (should get both kernel-produced and user-produced data)
  int total_consumed = 0;
  auto consume_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS / 100; i++) {
      ssize_t n = ::read(fd, read_buf, 256);
      if (n > 0) {
        total_consumed += n;
      }
    }
  });
  
  std::cout << "Kernel consumed " << total_consumed << " bytes via read()\n";
  print_metrics("Read ", consume_duration, NUM_ITERATIONS / 100);
  double read_latency = static_cast<double>(consume_duration.count()) / (NUM_ITERATIONS / 100);

  ::close(fd);
  return {write_latency, read_latency};
}

// Test 2: User produces AND consumes via mmap (zero-copy)
double test_mmap_produce_consume() {
  std::cout << "\n=== Test 2: mmap Producer-Consumer (User produces AND consumes) ===\n";

  int fd = ::open("/dev/mydevice_bypass", O_RDWR);
  if (fd < 0) {
    std::cerr << "Failed to open device\n";
    return 0.0;
  }

  // Map shared memory
  void* mapped = ::mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mapped == MAP_FAILED) {
    std::cerr << "mmap failed\n";
    ::close(fd);
    return 0.0;
  }

  volatile shared_control* ctrl = static_cast<volatile shared_control*>(mapped);
  volatile char* buffer = static_cast<volatile char*>(mapped) + CONTROL_SIZE;

  std::cout << "Initial state:\n";
  std::cout << "  Kernel produced: " << ctrl->kernel_produced.load() 
            << ", consumed: " << ctrl->kernel_consumed.load() << "\n";
  std::cout << "  User produced: " << ctrl->user_produced.load()
            << ", consumed: " << ctrl->user_consumed.load() << "\n";
  std::cout << "  Queue size: " << queue_available_data((const shared_control*)ctrl) << " items\n";

  // Let kernel producer fill the queue a bit
  std::cout << "Waiting for kernel producer...\n";
  ::sleep(1);

  std::cout << "After 1 second:\n";
  std::cout << "  Queue size: " << queue_available_data((const shared_control*)ctrl) << " items\n";

  // User consumer thread - consumes data from queue
  std::atomic<int> user_consumed_count{0};
  std::atomic<bool> stop_consumer{false};
  
  std::thread consumer([&]() {
    while (!stop_consumer.load()) {
      if (!queue_is_empty((const shared_control*)ctrl)) {
        int read_idx = ctrl->read_idx.load(std::memory_order_acquire);
        volatile char value = buffer[read_idx];  // Read data
        (void)value;  // Use it
        
        // Advance read index
        int next_read = (read_idx + 1) % QUEUE_SIZE;
        ctrl->read_idx.store(next_read, std::memory_order_release);
        ctrl->user_consumed.fetch_add(1, std::memory_order_relaxed);
        user_consumed_count++;
      } else {
        std::this_thread::yield();
      }
    }
  });

  // User producer - produces data into queue
  int user_produced_count = 0;
  auto produce_duration = time_benchmark([&] {
    for (int i = 0; i < NUM_ITERATIONS && !queue_is_full((const shared_control*)ctrl); i++) {
      int write_idx = ctrl->write_idx.load(std::memory_order_acquire);
      
      // Write data
      buffer[write_idx] = 'U';  // User-produced marker
      
      // Advance write index
      int next_write = (write_idx + 1) % QUEUE_SIZE;
      ctrl->write_idx.store(next_write, std::memory_order_release);
      ctrl->user_produced.fetch_add(1, std::memory_order_relaxed);
      user_produced_count++;
    }
  });

  std::cout << "\nUser produced " << user_produced_count << " items via mmap (zero-copy)\n";
  print_metrics("Produce", produce_duration, user_produced_count);
  double mmap_latency = static_cast<double>(produce_duration.count()) / user_produced_count;

  // Let consumer finish
  ::sleep(2);
  stop_consumer = true;
  consumer.join();

  std::cout << "User consumed " << user_consumed_count.load() << " items via mmap\n";

  std::cout << "\nFinal state:\n";
  std::cout << "  Kernel produced: " << ctrl->kernel_produced.load() 
            << ", consumed: " << ctrl->kernel_consumed.load() << "\n";
  std::cout << "  User produced: " << ctrl->user_produced.load()
            << ", consumed: " << ctrl->user_consumed.load() << "\n";
  std::cout << "  Queue size: " << queue_available_data((const shared_control*)ctrl) << " items\n";

  ::munmap(const_cast<shared_control*>((const shared_control*)ctrl), 4096);
  ::close(fd);
  
  return mmap_latency;
}

int main() {
  std::cout << "========================================\n";
  std::cout << "Producer-Consumer Queue Benchmark\n";
  std::cout << "========================================\n";
  std::cout << "Queue Size: " << QUEUE_SIZE << " items\n";
  std::cout << "Iterations: " << NUM_ITERATIONS << "\n";
  
  auto [write_lat, read_lat] = test_syscall_produce_consume();
  double syscall_avg_lat = (write_lat + read_lat) / 2.0;
  
  std::cout << "\n--- Sleeping 2 seconds before next test ---\n";
  ::sleep(2);
  
  double mmap_lat = test_mmap_produce_consume();
  
  std::cout << "\n========================================\n";
  std::cout << "Performance Summary\n";
  std::cout << "========================================\n";
  std::cout << "\n📊 LATENCY COMPARISON:\n";
  std::cout << "  Syscall (write):  " << std::fixed << std::setprecision(2) << write_lat << " ns\n";
  std::cout << "  Syscall (read):   " << std::fixed << std::setprecision(2) << read_lat << " ns\n";
  std::cout << "  Syscall (avg):    " << std::fixed << std::setprecision(2) << syscall_avg_lat << " ns\n";
  std::cout << "  mmap (zero-copy): " << std::fixed << std::setprecision(2) << mmap_lat << " ns\n";
  std::cout << "  ⚡ Speedup:        " << std::fixed << std::setprecision(1) 
            << (syscall_avg_lat / mmap_lat) << "x faster with mmap!\n";
  
  std::cout << "\n📈 THROUGHPUT COMPARISON:\n";
  double syscall_throughput = 1'000'000'000.0 / syscall_avg_lat / 1'000'000.0;
  double mmap_throughput = 1'000'000'000.0 / mmap_lat / 1'000'000.0;
  std::cout << "  Syscall:     " << std::fixed << std::setprecision(2) << syscall_throughput << " M ops/sec\n";
  std::cout << "  mmap:        " << std::fixed << std::setprecision(2) << mmap_throughput << " M ops/sec\n";
  std::cout << "  🚀 Gain:      +" << std::fixed << std::setprecision(2) 
            << (mmap_throughput - syscall_throughput) << " M ops/sec\n";
  
  std::cout << "\n========================================\n";
  std::cout << "Check 'dmesg' for kernel statistics\n";
  std::cout << "========================================\n";
  
  return 0;
}
