# Quick Start Guide

## 3-Step Setup for Full Networking in TempleOS

### Step 1: Build the Host Daemon
```bash
cd /workspace/NeonBridge
./build.sh
```
✓ Creates `Bin/neon_bridge` (Linux daemon)

### Step 2: Start QEMU with TempleOS + Shared Memory
```bash
qemu-system-x86_64 \
  -bios TempleOS.iso \
  -m 512M \
  -object memory-backend-file,id=neon_shm,mem-path=/dev/shm/neon_bridge,size=2M \
  -device ivshmem-plain,memdev=neon_shm \
  -netdev user,id=net0 \
  -device e1000,netdev=net0
```

### Step 3: Start the Bridge & Use Networking
**Terminal 1** (Host):
```bash
sudo ./Bin/neon_bridge
```

**TempleOS** (Guest):
```holyC
#include "Home:NeonLib.HC"

// Download a webpage
U8 *html = NeonHTTPGet("http://example.com/");
Printf("%s\n", html);
Free(html);
```

## What You Can Now Do

✅ **Download files**: Fetch any HTTP resource
✅ **Web browsing**: Build HTML renderer on top of NeonHTTPGet()
✅ **API calls**: REST, JSON APIs from HolyC
✅ **Package manager**: Download and install HolyC libraries
✅ **WiFi access**: Through host's WiFi adapter (no drivers needed in TempleOS!)

## Architecture Advantage

Instead of writing 100,000+ lines of TCP/IP code:
- TempleOS apps call simple HolyC functions
- Linux handles DNS, TCP, TLS, routing, drivers
- Zero-copy shared memory = near-native performance
- Works with ANY hardware Linux supports

This is the **fastest path to full networking** in TempleOS.
