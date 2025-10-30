#include <linux/fs.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/wait.h>
#include <linux/poll.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("truongnguyen");
MODULE_DESCRIPTION("a toy character device driver with mmap (kernel-bypass)");

#define DEVICE_NAME "mydevice_bypass"

static int major;
static char *buffer;
static size_t read_cursor;
static size_t write_cursor;
static DECLARE_WAIT_QUEUE_HEAD(read_queue);
static DECLARE_WAIT_QUEUE_HEAD(write_queue);

static int mydriver_open(struct inode *, struct file *);
static int mydriver_release(struct inode *, struct file *);
static ssize_t mydriver_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t mydriver_write(struct file *, const char __user *, size_t, loff_t *);
static int mydriver_mmap(struct file *, struct vm_area_struct *);
static unsigned int mydriver_poll(struct file *, struct poll_table_struct *);

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mydriver_open,
    .release = mydriver_release,
    .read = mydriver_read,
    .write = mydriver_write,
    .mmap = mydriver_mmap,
    .poll = mydriver_poll,
};

static void vm_open(struct vm_area_struct *vma) {
  printk(DEVICE_NAME ">>> vma opened\n");
}

static void vm_close(struct vm_area_struct *vma) {
  printk(DEVICE_NAME ">>> vma closed\n");
}

static const struct vm_operations_struct vmops = {
    .open = vm_open,
    .close = vm_close,
};

static int mydriver_mmap(struct file *filp, struct vm_area_struct *vma) {
  unsigned long vsize = vma->vm_end - vma->vm_start;

  if (vsize > PAGE_SIZE) {
    printk(DEVICE_NAME ">>> mmap size too large (%lu > %lu)\n", vsize, PAGE_SIZE);
    return -EINVAL;
  }

  // Use vm_flags_set for newer kernels (6.3+)
  vm_flags_set(vma, VM_SHARED | VM_MAYREAD | VM_MAYWRITE);
  if (remap_vmalloc_range(vma, buffer, vma->vm_pgoff) < 0) {
    printk(DEVICE_NAME ">>> remap_vmalloc_range failed\n");
    return -EAGAIN;
  }

  vma->vm_ops = &vmops;
  if (vma->vm_ops && vma->vm_ops->open)
    vma->vm_ops->open(vma);

  printk(DEVICE_NAME ">>> mmap mapped %lu bytes to user\n", vsize);
  return 0;
}

static int __init mydriver_init(void) {
  major = register_chrdev(0, DEVICE_NAME, &fops);
  if (major < 0) {
    printk(DEVICE_NAME ">>> Failed to register a major number\n");
    return major;
  }

  printk(DEVICE_NAME ">>> Registered with major number %d\n", major);

  // in practice, driver should create device automatically with `class_create` and `driver_create`
  printk(DEVICE_NAME ">>> 'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, major);

  buffer = vmalloc_user(PAGE_SIZE);
  if (!buffer) {
    unregister_chrdev(major, DEVICE_NAME);
    printk(DEVICE_NAME ">>> Failed to allocate memory for the buffer\n");
    return -ENOMEM;
  }

  read_cursor = 0;
  write_cursor = 0;
  memset(buffer, 0, PAGE_SIZE);
  printk(DEVICE_NAME ">>> Device buffer allocated successfully.\n");

  return 0;
}

static void __exit mydriver_exit(void) {
  if (buffer) {
    vfree(buffer);
    buffer = NULL;
  }
  unregister_chrdev(major, DEVICE_NAME);
  printk(DEVICE_NAME ">>> Unregistered successfully.\n");
}

static int mydriver_open(struct inode *inodep, struct file *filep) {
  read_cursor = 0;
  write_cursor = 0;
  printk(DEVICE_NAME ">>> Device opened.\n");
  return 0;
}

static int mydriver_release(struct inode *inodep, struct file *filep) {
  printk(DEVICE_NAME ">>> Device closed.\n");
  return 0;
}

static ssize_t mydriver_read(struct file *filep, char __user *user_buffer, size_t len, loff_t *_offset) {
  size_t bytes_to_read;

  // Simple non-blocking behavior for benchmarking
  if (read_cursor >= write_cursor) {
    return 0;
  }

  bytes_to_read = min(len, write_cursor - read_cursor);

  if (copy_to_user(user_buffer, buffer + read_cursor, bytes_to_read) != 0) {
    return -EFAULT;
  }

  read_cursor += bytes_to_read;
  printk(DEVICE_NAME ">>> Read %zu bytes at offset %zu\n", bytes_to_read, read_cursor);
  
  // Wake up any waiting readers (if using blocking mode later)
  wake_up_interruptible(&read_queue);
  
  return bytes_to_read;
}

static ssize_t mydriver_write(struct file *filep, const char __user *user_buffer, size_t len, loff_t *_offset) {
  size_t bytes_to_write;

  if (write_cursor >= PAGE_SIZE) {
    return -ENOSPC;
  }

  bytes_to_write = min(len, PAGE_SIZE - write_cursor);

  if (copy_from_user(buffer + write_cursor, user_buffer, bytes_to_write) != 0) {
    return -EFAULT;
  }

  write_cursor += bytes_to_write;
  printk(DEVICE_NAME ">>> Wrote %zu bytes at offset %zu\n", bytes_to_write, write_cursor);
  
  // Wake up readers - new data is available!
  wake_up_interruptible(&read_queue);
  
  return bytes_to_write;
}

static unsigned int mydriver_poll(struct file *filep, struct poll_table_struct *wait) {
  unsigned int mask = 0;

  poll_wait(filep, &read_queue, wait);
  poll_wait(filep, &write_queue, wait);

  // Check if data is available for reading
  if (read_cursor < write_cursor) {
    mask |= POLLIN | POLLRDNORM;  // Readable
  }

  // Check if space is available for writing
  if (write_cursor < PAGE_SIZE) {
    mask |= POLLOUT | POLLWRNORM; // Writable
  }

  return mask;
}

module_init(mydriver_init);
module_exit(mydriver_exit);
