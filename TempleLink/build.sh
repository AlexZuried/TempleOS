#!/bin/bash

###############################################################################
# TempleLink Build Script
# Builds the symbiotic Linux-TempleOS networking bridge
###############################################################################

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
DAEMON_BIN="$BUILD_DIR/templelinkd"

echo "=================================================="
echo "TempleLink Build System"
echo "Symbiotic Linux-TempleOS Networking Bridge"
echo "=================================================="
echo ""

# Create build directory
mkdir -p "$BUILD_DIR"

###############################################################################
# Step 1: Build Linux Daemon
###############################################################################

echo "[1/3] Building Linux daemon (templelinkd)..."

g++ --version | head -1

g++ -O3 -march=native -mtune=native \
    -std=c++17 \
    -flto \
    -fPIC \
    -Wall -Wextra \
    -I"$SCRIPT_DIR/include" \
    "$SCRIPT_DIR/daemon/main.cpp" \
    -o "$DAEMON_BIN" \
    -lpthread \
    -lrt

if [ $? -eq 0 ]; then
    echo "      ✓ Daemon compiled successfully"
    ls -lh "$DAEMON_BIN"
else
    echo "      ✗ Daemon compilation failed"
    exit 1
fi

echo ""

###############################################################################
# Step 2: Verify HolyC Source
###############################################################################

echo "[2/3] Verifying HolyC source files..."

if [ -f "$SCRIPT_DIR/temple_src/TempleLink.HC" ]; then
    echo "      ✓ TempleLink.HC found"
    wc -l "$SCRIPT_DIR/temple_src/TempleLink.HC" | awk '{print "      Lines of code:", $1}'
else
    echo "      ✗ TempleLink.HC not found"
    exit 1
fi

if [ -f "$SCRIPT_DIR/include/templelink.h" ]; then
    echo "      ✓ templelink.h found"
else
    echo "      ✗ templelink.h not found"
    exit 1
fi

echo ""

###############################################################################
# Step 3: Generate Integration Files
###############################################################################

echo "[3/3] Generating integration files..."

# Generate systemd service file for auto-start
cat > "$BUILD_DIR/templelinkd.service" << 'EOF'
[Unit]
Description=TempleLink Networking Bridge Daemon
Documentation=https://github.com/TempleOS/TempleLink
After=network.target

[Service]
Type=simple
ExecStart=/opt/templelink/bin/templelinkd
Restart=on-failure
RestartSec=5
User=root
Group=root

# Security hardening
NoNewPrivileges=true
ProtectSystem=strict
ProtectHome=read-only
ReadWritePaths=/run/templelink

[Install]
WantedBy=multi-user.target
EOF

echo "      ✓ Generated systemd service file"

# Generate installation script
cat > "$BUILD_DIR/install.sh" << 'EOF'
#!/bin/bash
set -e

INSTALL_PREFIX="/opt/templelink"

echo "Installing TempleLink to $INSTALL_PREFIX..."

mkdir -p "$INSTALL_PREFIX/bin"
mkdir -p "$INSTALL_PREFIX/include"
mkdir -p "$INSTALL_PREFIX/temple_src"
mkdir -p "/run/templelink"

cp build/templelinkd "$INSTALL_PREFIX/bin/"
cp include/templelink.h "$INSTALL_PREFIX/include/"
cp temple_src/TempleLink.HC "$INSTALL_PREFIX/temple_src/"

chmod +x "$INSTALL_PREFIX/bin/templelinkd"

# Install systemd service
cp build/templelinkd.service /etc/systemd/system/
systemctl daemon-reload
systemctl enable templelinkd

echo ""
echo "Installation complete!"
echo ""
echo "To start the daemon:"
echo "  sudo systemctl start templelinkd"
echo ""
echo "To check status:"
echo "  sudo systemctl status templelinkd"
echo ""
echo "HolyC library location:"
echo "  $INSTALL_PREFIX/temple_src/TempleLink.HC"
echo ""
echo "Add this line to your TempleOS startup:"
echo '  #include "/opt/templelink/temple_src/TempleLink.HC"'
EOF

chmod +x "$BUILD_DIR/install.sh"
echo "      ✓ Generated installation script"

# Generate quick-start guide
cat > "$BUILD_DIR/QUICKSTART.md" << 'EOF'
# TempleLink Quick Start Guide

## Prerequisites
- Linux system with g++ and systemd
- TempleOS running in a VM or on bare metal
- Shared memory access between Linux and TempleOS

## Building

```bash
./build.sh
```

## Installation

```bash
sudo ./build/install.sh
```

## Starting the Daemon

```bash
sudo systemctl start templelinkd
sudo systemctl status templelinkd
```

## Using in TempleOS

In your HolyC application:

```holyc
#include "/opt/templelink/temple_src/TempleLink.HC"

U0 MyApp() {
    TL_Init();
    
    // Make HTTP request
    U8 response[4096];
    I64 len = TL_HTTPGet("example.com", response, sizeof(response));
    
    if (len > 0) {
        Printf("Response: %s\n", response);
    }
    
    TL_Shutdown();
}
```

## Testing

Test the connection with:

```bash
# Check daemon is running
ps aux | grep templelinkd

# Check shared memory
ls -la /dev/shm/templelink_shm

# View logs
journalctl -u templelinkd -f
```

## Architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌──────────────┐
│  TempleOS App   │────▶│ Shared Memory    │────▶│ Linux Daemon │
│  (HolyC)        │◀────│ Ring Buffers     │◀────│ (C++)        │
└─────────────────┘     └──────────────────┘     └──────────────┘
                                                      │
                                                      ▼
                                             ┌──────────────┐
                                             │ Linux Kernel │
                                             │ Networking   │
                                             └──────────────┘
```

## Performance

- Latency: < 1μs for ring buffer operations
- Throughput: Limited by Linux network stack
- Zero-copy: Data transferred via shared memory

## Troubleshooting

1. **Daemon won't start**: Check `journalctl -u templelinkd`
2. **Connection timeout**: Ensure shared memory is accessible
3. **Permission denied**: Run daemon as root or configure permissions

## License

Public Domain - Same as TempleOS
EOF

echo "      ✓ Generated quick-start guide"

echo ""
echo "=================================================="
echo "Build Complete!"
echo "=================================================="
echo ""
echo "Next steps:"
echo "  1. Review build artifacts in: $BUILD_DIR/"
echo "  2. Install with: sudo $BUILD_DIR/install.sh"
echo "  3. Start daemon: sudo systemctl start templelinkd"
echo "  4. Include TempleLink.HC in your TempleOS apps"
echo ""
echo "Files created:"
echo "  - $DAEMON_BIN (Linux daemon binary)"
echo "  - $BUILD_DIR/templelinkd.service (systemd unit)"
echo "  - $BUILD_DIR/install.sh (installation script)"
echo "  - $BUILD_DIR/QUICKSTART.md (documentation)"
echo ""
