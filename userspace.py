import os, mmap

DEVICE = "/dev/mydevice"
BUFFER_SIZE = 4096

fd = os.open(DEVICE, os.O_RDWR)
with mmap.mmap(fd, BUFFER_SIZE, mmap.MAP_SHARED, mmap.PROT_READ | mmap.PROT_WRITE) as m:
    m.write(b"hello from python mmap!\n")
    text = m[11:17]
    print(text)
    assert text == b'python'
os.close(fd)
