#!/bin/bash
# NeonBridge Build System - Compiles Host Daemon and prepares Guest Library
set -e

echo "=== NeonBridge Build System ==="
echo "Building Host Network Daemon (Linux Native)..."

# Create build directory
mkdir -p /workspace/NeonBridge/Bin

# Compile the Host Daemon with optimizations
g++ -O3 -march=native -std=c++20 \
    -pthread \
    -I/workspace/NeonBridge/Host \
    /workspace/NeonBridge/Host/neon_host.cpp \
    -o /workspace/NeonBridge/Bin/neon_bridge \
    -lpthread

echo "✓ Host Daemon compiled successfully: /workspace/NeonBridge/Bin/neon_bridge"

# Verify binary
file /workspace/NeonBridge/Bin/neon_bridge

# Prepare Guest files for TempleOS integration
echo ""
echo "Preparing Guest Library for TempleOS..."
echo "Copy the following files to your TempleOS Home directory or Apps folder:"
echo "  - /workspace/NeonBridge/Guest/NeonLib.HC"
echo "  - /workspace/NeonBridge/Guest/NeonDemo.HC"
echo ""
echo "=== Build Complete ==="
echo "Next Steps:"
echo "1. Run './run_demo.sh' to test the bridge locally"
echo "2. Copy Guest files to TempleOS and include 'NeonLib.HC' in your apps"
echo "3. Start the daemon: sudo ./Bin/neon_bridge"
