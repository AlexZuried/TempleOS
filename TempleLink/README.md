# TempleLink: Symbiotic Linux-TempleOS Networking Bridge

## Architecture Overview

TempleLink creates a hybrid symbiotic relationship between Linux and TempleOS:
- **Linux Side**: Runs a high-performance daemon (`templelinkd`) that handles all actual networking using the mature Linux stack
- **TempleOS Side**: Provides a lightweight HolyC library that communicates with the daemon via shared memory/Unix sockets
- **Result**: TempleOS applications get full Linux networking capabilities transparently, while maintaining HolyC syntax

## Directory Structure
```
TempleLink/
├── daemon/           # Linux C++ daemon (high performance networking)
│   ├── main.cpp      # Main event loop
│   ├── socket_mgr.cpp # Socket management
│   └── packet_proc.cpp # Zero-copy packet processing
├── include/          # Shared header definitions
│   └── templelink.h  # Protocol definitions
├── temple_src/       # HolyC source for TempleOS side
│   └── TempleLink.HC # HolyC library
├── build.sh          # Build script
└── README.md         # This file
```

## How It Works

1. **Startup**: `templelinkd` starts on Linux, creates shared memory region
2. **TempleOS Boot**: HolyC library initializes, maps shared memory
3. **Socket Call**: TempleOS app calls `TL_Socket()` → writes to shared ring buffer
4. **Linux Processing**: Daemon reads request, executes real Linux syscall
5. **Response**: Result written back to ring buffer, interrupt signaled
6. **Transparency**: TempleOS app thinks it's doing native networking

## Performance Features

- **Zero-Copy**: Shared memory eliminates data copying between systems
- **Lock-Free Ring Buffers**: Nanosecond-level communication latency
- **Batch Processing**: Multiple socket ops bundled in single transition
- **Direct IRQ Bypass**: Daemon can signal TempleOS VM directly

## Building

```bash
./build.sh
```

This compiles:
1. Linux daemon with optimizations (-O3, AVX2)
2. HolyC library (to be included in TempleOS build)

## Usage in TempleOS

```holyc
// Instead of non-existent native networking:
// sock = Socket(AF_INET, SOCK_STREAM, 0);

// Use TempleLink (transparent wrapper):
sock = TL_Socket(AF_INET, SOCK_STREAM, 0);
TL_Connect(sock, "192.168.1.1", 80);
TL_Send(sock, "GET / HTTP/1.1\r\n...", 0);
buffer = TL_Recv(sock, buf_size);
```

## Benefits Over Pure Linux Networking

1. **HolyC Simplicity**: Keep HolyC's elegant syntax while getting Linux power
2. **Security Isolation**: Network stack runs in separate Linux process
3. **Live Updates**: Update networking daemon without rebooting TempleOS
4. **Hybrid Debugging**: Use Linux tools (tcpdump, strace) on TempleOS network traffic
5. **Gradual Migration**: Can slowly replace Linux calls with native HolyC as implemented

## Files Included

- Complete C++ daemon with epoll-based event loop
- Lock-free ring buffer implementation
- HolyC wrapper library with Linux-compatible API
- Build system with automatic detection
- Integration hooks for TempleOS build process
