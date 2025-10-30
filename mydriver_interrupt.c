#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("truongnguyen");
MODULE_DESCRIPTION("a toy character device driver");

#define DEVICE_NAME "mydevice_interrupt"
#define BUFFER_SIZE 1024

static int major;
static char *buffer;
static size_t read_cursor;
static size_t write_cursor;

static int mydriver_open(struct inode *, struct file *);
static int mydriver_release(struct inode *, struct file *);
static ssize_t mydriver_read(struct file *, char __user *, size_t, loff_t *);
static ssize_t mydriver_write(struct file *, const char __user *, size_t, loff_t *);

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mydriver_open,
    .release = mydriver_release,
    .read = mydriver_read,
    .write = mydriver_write,
};

static int __init mydriver_init(void) {
  major = register_chrdev(0, DEVICE_NAME, &fops);
  if (major < 0) {
    printk(DEVICE_NAME ">>> Failed to register a major number\n");
    return major;
  }

  printk(DEVICE_NAME ">>> Registered with major number %d\n", major);

  // in practice, driver should create device automatically with `class_create` and `driver_create`
  printk(DEVICE_NAME ">>> 'mknod /dev/%s c %d 0'.\n", DEVICE_NAME, major);

  buffer = kmalloc(BUFFER_SIZE, GFP_KERNEL);
  if (!buffer) {
    unregister_chrdev(major, DEVICE_NAME);
    printk(DEVICE_NAME ">>> Failed to allocate memory for the buffer\n");
    return -ENOMEM;
  }

  read_cursor = 0;
  write_cursor = 0;
  memset(buffer, 0, BUFFER_SIZE);
  printk(DEVICE_NAME ">>> Device buffer allocated successfully.\n");

  return 0;
}

static void __exit mydriver_exit(void) {
  if (buffer) {
    kfree(buffer);
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

  if (read_cursor >= write_cursor) {
    return 0;
  }

  bytes_to_read = min(len, write_cursor - read_cursor);

  if (copy_to_user(user_buffer, buffer + read_cursor, bytes_to_read) != 0) {
    return -EFAULT;
  }

  read_cursor += bytes_to_read;
  printk(DEVICE_NAME ">>> Read %zu bytes at offset %zu\n", bytes_to_read, read_cursor);
  return bytes_to_read;
}

static ssize_t mydriver_write(struct file *filep, const char __user *user_buffer, size_t len, loff_t *_offset) {
  size_t bytes_to_write;

  if (write_cursor >= BUFFER_SIZE) {
    return -ENOSPC;
  }

  bytes_to_write = min(len, BUFFER_SIZE - write_cursor);

  if (copy_from_user(buffer + write_cursor, user_buffer, bytes_to_write) != 0) {
    return -EFAULT;
  }

  write_cursor += bytes_to_write;
  printk(DEVICE_NAME ">>> Wrote %zu bytes at offset %zu\n", bytes_to_write, write_cursor);
  return bytes_to_write;
}

module_init(mydriver_init);
module_exit(mydriver_exit);
