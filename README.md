This is a kernel module for exposing a safe file operations API to enable safe, reloadable Rust file systems.

It can be used by Linux kernel file systems implemented using the bento Rust crate found at <https://gitlab.cs.washington.edu/sm237/bento>.

Runs in Linux kernel version 4.15.

This is based on the FUSE kernel module from Linux kernel version 4.15.

**To compile:**
```
make
```

**To clean:**
```
make clean
```

**To insert module:**
```
sudo insmod bentofs.ko
```

**To remove module:**
```
sudo rmmod bentofs
```
