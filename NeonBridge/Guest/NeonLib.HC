// NeonBridge Guest Library (TempleOS HolyC)
// Provides Linux-compatible networking API to TempleOS applications
// Uses shared memory ring buffers to communicate with host daemon

#Define NEON_MAGIC 0x4E454F4E
#Define RING_SIZE 1024
#Define MAX_SOCKETS 64

// Packet types
#Define NEON_PKT_DATA    0
#Define NEON_PKT_CONNECT 1
#Define NEON_PKT_CLOSE   2
#Define NEON_PKT_ERROR   3

Class NeonPacket {
    U32 socket_id;
    U32 length;
    U8 type;
    U8 data[1460];
};

Class NeonRingBuffer {
    U32 magic;
    U32 version;
    U64 head;
    U64 tail;
    U64 resp_head;
    U64 resp_tail;
    NeonPacket packets[RING_SIZE];
};

// Global ring buffer pointer (mapped from shared memory)
NeonRingBuffer *g_neon_ring = NULL;
Bool g_neon_initialized = FALSE;

// Socket state tracking
Class NeonSocket {
    Bool active;
    Bool connected;
    Bool has_error;
    U8 pending_data[4096];
    U32 pending_len;
};

NeonSocket g_sockets[MAX_SOCKETS];

// ============================================================================
// INITIALIZATION
// ============================================================================

// Map shared memory from host
// In QEMU: use -device ivshmem-plain,memdev=neon_shm
Bool NeonInit() {
    if (g_neon_initialized) return TRUE;
    
    // Try to map shared memory region
    // This requires QEMU ivshmem device or similar
    // Address would be configured via QEMU command line
    U64 shm_addr = 0xF0000000; // Example fixed address for ivshmem
    
    g_neon_ring = (NeonRingBuffer *)shm_addr;
    
    if (g_neon_ring->magic != NEON_MAGIC) {
        Printf("NeonBridge: Invalid magic number. Is host daemon running?\n");
        return FALSE;
    }
    
    // Initialize socket table
    MemSet(g_sockets, 0, SizeOf(g_sockets));
    
    g_neon_initialized = TRUE;
    Printf("NeonBridge: Connected to host daemon (version %d)\n", g_neon_ring->version);
    return TRUE;
}

Void NeonShutdown() {
    // Close all active sockets
    For (U32 i = 0; i < MAX_SOCKETS; i++) {
        If (g_sockets[i].active) {
            NeonClose(i);
        }
    }
    g_neon_initialized = FALSE;
}

// ============================================================================
// SOCKET API
// ============================================================================

// Allocate a new socket ID
Int NeonSocket() {
    If (!g_neon_initialized && !NeonInit()) return -1;
    
    For (U32 i = 1; i < MAX_SOCKETS; i++) {
        If (!g_sockets[i].active) {
            g_sockets[i].active = TRUE;
            g_sockets[i].connected = FALSE;
            g_sockets[i].has_error = FALSE;
            g_sockets[i].pending_len = 0;
            Return i;
        }
    }
    Return -1; // No free sockets
}

// Connect to remote host
// host: "example.com" or "192.168.1.1"
// port: 80, 443, etc.
// Returns: 0 on success (or in-progress), -1 on error
Int NeonConnect(Int sock_id, Char *host, Int port) {
    If (sock_id <= 0 || sock_id >= MAX_SOCKETS) Return -1;
    If (!g_sockets[sock_id].active) Return -1;
    If (!g_neon_initialized) Return -1;
    
    // Prepare connect request
    U64 idx = g_neon_ring->resp_head % RING_SIZE;
    NeonPacket *pkt = &g_neon_ring->packets[idx];
    
    pkt->socket_id = sock_id;
    pkt->type = NEON_PKT_CONNECT;
    StrLen(host) > 1459 ? 1459 : StrLen(host); // Clamp length
    MemCopy(pkt->data, host, StrLen(host));
    // Note: In real impl, encode port in packet too
    
    // Notify host
    __sync_fetch_and_add(&g_neon_ring->resp_head, 1);
    
    // Wait for response (simplified - should be async in real impl)
    // Real impl would use callbacks or polling
    Return 0;
}

// Send data
// Returns bytes sent, or -1 on error
Int NeonSend(Int sock_id, U8 *data, Int len) {
    If (sock_id <= 0 || sock_id >= MAX_SOCKETS) Return -1;
    If (!g_sockets[sock_id].active) Return -1;
    If (!g_neon_initialized) Return -1;
    
    // For now, just queue data (real impl needs proper send mechanism)
    // This is a placeholder
    Return len;
}

// Receive data
// Returns bytes read, 0 if no data, -1 on error
Int NeonRecv(Int sock_id, U8 *buffer, Int max_len) {
    If (sock_id <= 0 || sock_id >= MAX_SOCKETS) Return -1;
    If (!g_sockets[sock_id].active) Return -1;
    If (!g_neon_initialized) Return -1;
    
    // Check if we have pending data
    If (g_sockets[sock_id].pending_len > 0) {
        Int to_copy = min(max_len, g_sockets[sock_id].pending_len);
        MemCopy(buffer, g_sockets[sock_id].pending_data, to_copy);
        
        // Shift remaining data
        If (to_copy < g_sockets[sock_id].pending_len) {
            MemMove(g_sockets[sock_id].pending_data, 
                   g_sockets[sock_id].pending_data + to_copy,
                   g_sockets[sock_id].pending_len - to_copy);
        }
        g_sockets[sock_id].pending_len -= to_copy;
        Return to_copy;
    }
    
    // Check ring buffer for new data
    While (g_neon_ring->tail != g_neon_ring->head) {
        U64 idx = g_neon_ring->tail % RING_SIZE;
        NeonPacket *pkt = &g_neon_ring->packets[idx];
        
        If (pkt->socket_id == sock_id) {
            If (pkt->type == NEON_PKT_DATA) {
                // Copy to pending buffer
                If (pkt->length <= SizeOf(g_sockets[sock_id].pending_data)) {
                    MemCopy(g_sockets[sock_id].pending_data, pkt->data, pkt->length);
                    g_sockets[sock_id].pending_len = pkt->length;
                    
                    // Now recurse to return the data
                    Return NeonRecv(sock_id, buffer, max_len);
                }
            } Else If (pkt->type == NEON_PKT_CLOSE) {
                g_sockets[sock_id].connected = FALSE;
                g_sockets[sock_id].active = FALSE;
                Return 0; // EOF
            } Else If (pkt->type == NEON_PKT_ERROR) {
                g_sockets[sock_id].has_error = TRUE;
                Return -1;
            }
        }
        
        __sync_fetch_and_add(&g_neon_ring->tail, 1);
    }
    
    Return 0; // No data available
}

// Close socket
Void NeonClose(Int sock_id) {
    If (sock_id <= 0 || sock_id >= MAX_SOCKETS) Return;
    If (!g_sockets[sock_id].active) Return;
    
    // Notify host
    U64 idx = g_neon_ring->resp_head % RING_SIZE;
    NeonPacket *pkt = &g_neon_ring->packets[idx];
    pkt->socket_id = sock_id;
    pkt->type = NEON_PKT_CLOSE;
    pkt->length = 0;
    __sync_fetch_and_add(&g_neon_ring->resp_head, 1);
    
    g_sockets[sock_id].active = FALSE;
    g_sockets[sock_id].connected = FALSE;
}

// ============================================================================
// HIGH-LEVEL HTTP CLIENT
// ============================================================================

// Simple HTTP GET request
// Returns response body (caller must free)
Char *NeonHTTPGet(Char *url) {
    // Parse URL
    Char host[256], path[256];
    Int port = 80;
    
    // Simplified parsing (real impl needs full URL parser)
    SScanf(url, "http://%[^:/]:%d/%s", host, &port, path);
    If (!host[0]) {
        SScanf(url, "http://%[^/]/%s", host, path);
        port = 80;
    }
    
    Int sock = NeonSocket();
    If (sock < 0) Return NULL;
    
    If (NeonConnect(sock, host, port) < 0) {
        NeonClose(sock);
        Return NULL;
    }
    
    // Build HTTP request
    Char request[1024];
    SPrintf(request, "GET /%s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path, host);
    
    NeonSend(sock, (U8 *)request, StrLen(request));
    
    // Read response (simplified - real impl needs chunked encoding support)
    U8 buffer[4096];
    Char *response = Malloc(65536);
    Int total = 0;
    
    While (TRUE) {
        Int n = NeonRecv(sock, buffer, SizeOf(buffer));
        If (n <= 0) Break;
        
        If (total + n >= 65536) {
            response = Realloc(response, total + n + 1);
        }
        MemCopy(response + total, buffer, n);
        total += n;
    }
    response[total] = 0;
    
    NeonClose(sock);
    Return response;
}

// ============================================================================
// DEMO / TEST FUNCTIONS
// ============================================================================

Void NeonDemo() {
    Printf("\n=== NeonBridge Demo ===\n");
    
    If (!NeonInit()) {
        Printf("Failed to initialize NeonBridge. Is host daemon running?\n");
        Return;
    }
    
    // Test HTTP GET
    Printf("Fetching https://example.com...\n");
    Char *response = NeonHTTPGet("http://example.com/");
    
    If (response) {
        Printf("Response received (%d bytes):\n", StrLen(response));
        Printf("%s\n", response);
        Free(response);
    } Else {
        Printf("Failed to fetch URL\n");
    }
    
    NeonShutdown();
    Printf("Demo complete.\n");
}
