#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("truongnguyen");
MODULE_DESCRIPTION("Producer-Consumer character device driver with circular queue");

#define DEVICE_NAME "mydevice_bypass"
#define PAGE_SIZE_BYTES 4096
#define CONTROL_SIZE 64
#define QUEUE_SIZE (PAGE_SIZE_BYTES - CONTROL_SIZE)

static int major;
static void *shared_buffer;
static struct task_struct *producer_thread;
static struct task_struct *consumer_thread;
static bool stop_threads = false;

// Shared control structure (must match userspace definition)
struct shared_control {
    atomic_t write_idx;
    atomic_t read_idx;
    atomic_t kernel_produced;
    atomic_t kernel_consumed;
    atomic_t user_produced;
    atomic_t user_consumed;
};

static struct shared_control *ctrl;
static char *queue_buffer;

// Queue operations
static inline int queue_available_data(void) {
    int write_idx = atomic_read(&ctrl->write_idx);
    int read_idx = atomic_read(&ctrl->read_idx);
    return (write_idx - read_idx + QUEUE_SIZE) % QUEUE_SIZE;
}

static inline int queue_available_space(void) {
    return QUEUE_SIZE - queue_available_data() - 1;
}

static inline bool queue_is_empty(void) {
    return queue_available_data() == 0;
}

static inline bool queue_is_full(void) {
    return queue_available_space() == 0;
}

// Kernel producer thread - continuously produces data
static int kernel_producer(void *data) {
    int count = 0;
    
    printk(KERN_INFO DEVICE_NAME ": Kernel producer thread started\n");
    
    while (!kthread_should_stop() && !stop_threads) {
        if (!queue_is_full()) {
            int write_idx = atomic_read(&ctrl->write_idx);
            
            // Produce data - cycling through 'A' to 'Z'
            queue_buffer[write_idx] = 'A' + (count % 26);
            
            // Advance write index with memory barrier
            smp_wmb();
            atomic_set(&ctrl->write_idx, (write_idx + 1) % QUEUE_SIZE);
            atomic_inc(&ctrl->kernel_produced);
            
            count++;
            
            // Yield occasionally to prevent monopolizing CPU
            if (count % 1000 == 0) {
                usleep_range(100, 200);
            }
        } else {
            // Queue full, sleep a bit
            usleep_range(10, 50);
        }
    }
    
    printk(KERN_INFO DEVICE_NAME ": Kernel producer stopping. Produced: %d items\n", 
           atomic_read(&ctrl->kernel_produced));
    return 0;
}

// Kernel consumer thread - continuously consumes data
static int kernel_consumer(void *data) {
    int count = 0;
    
    printk(KERN_INFO DEVICE_NAME ": Kernel consumer thread started\n");
    
    while (!kthread_should_stop() && !stop_threads) {
        if (!queue_is_empty()) {
            int read_idx = atomic_read(&ctrl->read_idx);
            
            // Consume data
            char value = queue_buffer[read_idx];
            (void)value;  // Use the value
            
            // Advance read index with memory barrier
            smp_wmb();
            atomic_set(&ctrl->read_idx, (read_idx + 1) % QUEUE_SIZE);
            atomic_inc(&ctrl->kernel_consumed);
            
            count++;
            
            // Yield occasionally to prevent monopolizing CPU
            if (count % 1000 == 0) {
                usleep_range(100, 200);
            }
        } else {
            // Queue empty, sleep a bit
            usleep_range(10, 50);
        }
    }
    
    printk(KERN_INFO DEVICE_NAME ": Kernel consumer stopping. Consumed: %d items\n",
           atomic_read(&ctrl->kernel_consumed));
    return 0;
}

static int mydriver_open(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO DEVICE_NAME ": Device opened\n");
    return 0;
}

static int mydriver_release(struct inode *inodep, struct file *filep) {
    printk(KERN_INFO DEVICE_NAME ": Device closed\n");
    printk(KERN_INFO DEVICE_NAME ": Final stats - Kernel produced: %d, consumed: %d\n",
           atomic_read(&ctrl->kernel_produced), atomic_read(&ctrl->kernel_consumed));
    printk(KERN_INFO DEVICE_NAME ": Final stats - User produced: %d, consumed: %d\n",
           atomic_read(&ctrl->user_produced), atomic_read(&ctrl->user_consumed));
    return 0;
}

static ssize_t mydriver_read(struct file *filep, char __user *user_buffer, 
                             size_t len, loff_t *offset) {
    size_t bytes_read = 0;
    
    while (bytes_read < len && !queue_is_empty()) {
        int read_idx = atomic_read(&ctrl->read_idx);
        char value = queue_buffer[read_idx];
        
        if (copy_to_user(user_buffer + bytes_read, &value, 1) != 0) {
            return -EFAULT;
        }
        
        smp_wmb();
        atomic_set(&ctrl->read_idx, (read_idx + 1) % QUEUE_SIZE);
        atomic_inc(&ctrl->user_consumed);
        bytes_read++;
    }
    
    return bytes_read;
}

static ssize_t mydriver_write(struct file *filep, const char __user *user_buffer,
                              size_t len, loff_t *offset) {
    size_t bytes_written = 0;
    
    while (bytes_written < len && !queue_is_full()) {
        int write_idx = atomic_read(&ctrl->write_idx);
        char value;
        
        if (copy_from_user(&value, user_buffer + bytes_written, 1) != 0) {
            return -EFAULT;
        }
        
        queue_buffer[write_idx] = value;
        
        smp_wmb();
        atomic_set(&ctrl->write_idx, (write_idx + 1) % QUEUE_SIZE);
        atomic_inc(&ctrl->user_produced);
        bytes_written++;
    }
    
    return bytes_written;
}

static void vm_open(struct vm_area_struct *vma) {
    printk(KERN_INFO DEVICE_NAME ": VMA opened\n");
}

static void vm_close(struct vm_area_struct *vma) {
    printk(KERN_INFO DEVICE_NAME ": VMA closed\n");
}

static const struct vm_operations_struct vmops = {
    .open = vm_open,
    .close = vm_close,
};

static int mydriver_mmap(struct file *filp, struct vm_area_struct *vma) {
    unsigned long vsize = vma->vm_end - vma->vm_start;
    
    if (vsize > PAGE_SIZE_BYTES) {
        printk(KERN_ERR DEVICE_NAME ": mmap size too large (%lu > %d)\n", 
               vsize, PAGE_SIZE_BYTES);
        return -EINVAL;
    }
    
    vm_flags_set(vma, VM_SHARED | VM_MAYREAD | VM_MAYWRITE);
    
    if (remap_vmalloc_range(vma, shared_buffer, vma->vm_pgoff) < 0) {
        printk(KERN_ERR DEVICE_NAME ": remap_vmalloc_range failed\n");
        return -EAGAIN;
    }
    
    vma->vm_ops = &vmops;
    if (vma->vm_ops && vma->vm_ops->open)
        vma->vm_ops->open(vma);
    
    printk(KERN_INFO DEVICE_NAME ": mmap mapped %lu bytes to user\n", vsize);
    return 0;
}

static const struct file_operations fops = {
    .owner = THIS_MODULE,
    .open = mydriver_open,
    .release = mydriver_release,
    .read = mydriver_read,
    .write = mydriver_write,
    .mmap = mydriver_mmap,
};

static int __init mydriver_init(void) {
    major = register_chrdev(0, DEVICE_NAME, &fops);
    if (major < 0) {
        printk(KERN_ERR DEVICE_NAME ": Failed to register a major number\n");
        return major;
    }
    
    printk(KERN_INFO DEVICE_NAME ": Registered with major number %d\n", major);
    printk(KERN_INFO DEVICE_NAME ": Run 'mknod /dev/%s c %d 0'\n", DEVICE_NAME, major);
    
    // Allocate shared buffer
    shared_buffer = vmalloc_user(PAGE_SIZE_BYTES);
    if (!shared_buffer) {
        unregister_chrdev(major, DEVICE_NAME);
        printk(KERN_ERR DEVICE_NAME ": Failed to allocate shared buffer\n");
        return -ENOMEM;
    }
    
    memset(shared_buffer, 0, PAGE_SIZE_BYTES);
    
    // Setup control structure and queue buffer
    ctrl = (struct shared_control *)shared_buffer;
    queue_buffer = (char *)shared_buffer + CONTROL_SIZE;
    
    atomic_set(&ctrl->write_idx, 0);
    atomic_set(&ctrl->read_idx, 0);
    atomic_set(&ctrl->kernel_produced, 0);
    atomic_set(&ctrl->kernel_consumed, 0);
    atomic_set(&ctrl->user_produced, 0);
    atomic_set(&ctrl->user_consumed, 0);
    
    printk(KERN_INFO DEVICE_NAME ": Shared buffer allocated. Control size: %d, Queue size: %d\n",
           CONTROL_SIZE, QUEUE_SIZE);
    
    // Start kernel threads
    stop_threads = false;
    
    producer_thread = kthread_run(kernel_producer, NULL, "mydriver_producer");
    if (IS_ERR(producer_thread)) {
        vfree(shared_buffer);
        unregister_chrdev(major, DEVICE_NAME);
        printk(KERN_ERR DEVICE_NAME ": Failed to create producer thread\n");
        return PTR_ERR(producer_thread);
    }
    
    consumer_thread = kthread_run(kernel_consumer, NULL, "mydriver_consumer");
    if (IS_ERR(consumer_thread)) {
        stop_threads = true;
        kthread_stop(producer_thread);
        vfree(shared_buffer);
        unregister_chrdev(major, DEVICE_NAME);
        printk(KERN_ERR DEVICE_NAME ": Failed to create consumer thread\n");
        return PTR_ERR(consumer_thread);
    }
    
    printk(KERN_INFO DEVICE_NAME ": Kernel producer and consumer threads started\n");
    
    return 0;
}

static void __exit mydriver_exit(void) {
    // Stop threads
    stop_threads = true;
    
    if (producer_thread) {
        kthread_stop(producer_thread);
        printk(KERN_INFO DEVICE_NAME ": Producer thread stopped\n");
    }
    
    if (consumer_thread) {
        kthread_stop(consumer_thread);
        printk(KERN_INFO DEVICE_NAME ": Consumer thread stopped\n");
    }
    
    if (shared_buffer) {
        vfree(shared_buffer);
        shared_buffer = NULL;
    }
    
    unregister_chrdev(major, DEVICE_NAME);
    printk(KERN_INFO DEVICE_NAME ": Module unloaded\n");
}

module_init(mydriver_init);
module_exit(mydriver_exit);
