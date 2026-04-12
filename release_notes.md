# BoredOS 26.4 "Voyager"

**Welcome to BoredOS Voyager**, the next generation of the BoredOS kernel featuring significant architectural improvements and system-level refinements.

---

## Kernel Upgrade

The headline of this release is the **Kernel 4.0.0** upgrade, a major step forward from Kernel 3.2.3, bringing enhanced stability and performance improvements.

- **Kernel Version**: Boredkernel 4.0.0-stable
- **Major Focus**: Multi-processor reliability and VFS infrastructure

---

## Virtual File System (VFS)

A new VFS layer implementation, providing robust path normalization and mount management.

- **Path Normalization**: Proper handling of relative and absolute paths
- **Mount System**: Support for multiple filesystem mounts
- **Stability**: Comprehensive error handling and resource management

---

## System Introspection

New kernel introspection frameworks enabling real-time system monitoring:

- **SysFS**: Virtual filesystem exposing system information and device states
  - Graphics information (resolution, framebuffer details)
  - Memory tracking and allocation statistics
  - Module information
  
- **ProcFS**: Process filesystem with enhanced process information exposure

---

## Storage & Filesystem

Significant improvements to disk and filesystem handling:

- **FAT32 Enhancements**: Major refactor with improved file operations
- **AHCI Driver**: Better disk controller support and reliability
- **Disk Manager**: Refined disk operation handling

---

## Performance & Stability

- **SMP Improvements**: Enhanced multi-processor support and synchronization
- **Kernel Subsystem Architecture**: Reorganized for better modularity
- **Syscall System**: Refined syscall interface for better reliability

---

## User Interface & Tools

- **Task Manager**: Improved process viewing and management
- **File Explorer**: Removed legacy drive selector for streamlined operation
- **Standard Library**: Added `strstr()` and `strchr()` string functions

---



---

## Installation & Updates

To update to the latest version, pull the latest changes from the repository and rebuild your environment:

```bash
git pull
make clean
make
```

Or download the provided `.iso` from this release.

## 🔐 Security & Verification
If downloading from a third-party source, please verify your image.
SHA-256 Hash: ```55cca8f07b9570276afbdc8eb71b1a9c9a34ebd003ae9754a8371e00ece8e986```


---


