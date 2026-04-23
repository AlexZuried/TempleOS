#!/bin/bash
# Test the NeonBridge Host Daemon locally (without TempleOS)
# This simulates the shared memory communication

echo "=== NeonBridge Local Test ==="
echo "Starting host daemon in background..."

# Start the daemon
/workspace/NeonBridge/Bin/neon_bridge &
DAEMON_PID=$!
sleep 2

# Check if running
if ! kill -0 $DAEMON_PID 2>/dev/null; then
    echo "ERROR: Daemon failed to start"
    exit 1
fi

echo "✓ Daemon started (PID: $DAEMON_PID)"
echo ""
echo "Shared memory region created at /neon_bridge"
echo ""
echo "To test with TempleOS:"
echo "1. Start QEMU with ivshmem device pointing to /neon_bridge"
echo "2. Copy Guest/NeonLib.HC to TempleOS"
echo "3. Run NeonDemo() in TempleOS"
echo ""
echo "Press Ctrl+C to stop the daemon"
wait $DAEMON_PID
