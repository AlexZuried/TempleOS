/* 
 * NeonBridge Host Daemon (Linux Side)
 * Provides high-performance networking to TempleOS via shared memory ring buffers
 * Architecture: Zero-copy IPC with direct socket passthrough
 */

#include <iostream>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <pthread.h>
#include <fcntl.h>
#include <poll.h>
#include <vector>
#include <queue>
#include <atomic>

// ============================================================================
// SHARED MEMORY PROTOCOL DEFINITION
// ============================================================================

#define NEON_MAGIC 0x4E454F4E // "NEON"
#define RING_SIZE 1024
#define MAX_SOCKETS 64

#pragma pack(push, 1)

struct NeonPacket {
    uint32_t socket_id;
    uint32_t length;
    uint8_t type; // 0=DATA, 1=CONNECT, 2=CLOSE, 3=ERROR
    uint8_t data[1460]; // Max TCP segment
};

struct NeonRingBuffer {
    uint32_t magic;
    uint32_t version;
    uint64_t head; // Host writes here
    uint64_t tail; // Guest reads here
    uint64_t resp_head; // Guest writes responses
    uint64_t resp_tail; // Host reads responses
    NeonPacket packets[RING_SIZE];
};

struct NeonSocketInfo {
    int fd;
    bool active;
    char remote_ip[64];
    int remote_port;
};

#pragma pack(pop)

// ============================================================================
// GLOBAL STATE
// ============================================================================

static NeonRingBuffer* g_ring = nullptr;
static NeonSocketInfo g_sockets[MAX_SOCKETS];
static std::atomic<bool> g_running(true);
static pthread_mutex_t g_socket_mutex = PTHREAD_MUTEX_INITIALIZER;

// ============================================================================
// SOCKET MANAGEMENT
// ============================================================================

int allocate_socket() {
    pthread_mutex_lock(&g_socket_mutex);
    for (int i = 1; i < MAX_SOCKETS; i++) {
        if (!g_sockets[i].active) {
            g_sockets[i].active = true;
            g_sockets[i].fd = -1;
            pthread_mutex_unlock(&g_socket_mutex);
            return i;
        }
    }
    pthread_mutex_unlock(&g_socket_mutex);
    return -1;
}

void free_socket(int id) {
    if (id <= 0 || id >= MAX_SOCKETS) return;
    pthread_mutex_lock(&g_socket_mutex);
    if (g_sockets[id].active && g_sockets[id].fd >= 0) {
        close(g_sockets[id].fd);
    }
    g_sockets[id].active = false;
    g_sockets[id].fd = -1;
    pthread_mutex_unlock(&g_socket_mutex);
}

// ============================================================================
// NETWORK OPERATIONS
// ============================================================================

int handle_connect(uint32_t socket_id, const char* host, int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) return -1;

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Resolve hostname
    struct hostent* server = gethostbyname(host);
    if (!server) {
        close(sock);
        return -1;
    }
    memcpy(&addr.sin_addr.s_addr, server->h_addr, server->h_length);

    // Set non-blocking
    fcntl(sock, F_SETFL, O_NONBLOCK);

    int result = connect(sock, (struct sockaddr*)&addr, sizeof(addr));
    
    pthread_mutex_lock(&g_socket_mutex);
    g_sockets[socket_id].fd = sock;
    strncpy(g_sockets[socket_id].remote_ip, host, 63);
    g_sockets[socket_id].remote_port = port;
    pthread_mutex_unlock(&g_socket_mutex);

    return (result == 0 || errno == EINPROGRESS) ? 0 : -1;
}

int handle_send(uint32_t socket_id, uint8_t* data, uint32_t len) {
    if (socket_id >= MAX_SOCKETS || !g_sockets[socket_id].active) return -1;
    int fd = g_sockets[socket_id].fd;
    if (fd < 0) return -1;

    return send(fd, data, len, MSG_NOSIGNAL);
}

int handle_recv(uint32_t socket_id, uint8_t* buffer, uint32_t max_len) {
    if (socket_id >= MAX_SOCKETS || !g_sockets[socket_id].active) return -1;
    int fd = g_sockets[socket_id].fd;
    if (fd < 0) return -1;

    return recv(fd, buffer, max_len, MSG_DONTWAIT);
}

void handle_close(uint32_t socket_id) {
    free_socket(socket_id);
}

// ============================================================================
// RING BUFFER PROCESSING
// ============================================================================

void process_guest_requests() {
    while (g_ring->resp_tail != g_ring->resp_head) {
        uint64_t idx = g_ring->resp_tail % RING_SIZE;
        NeonPacket* pkt = &g_ring->packets[idx];
        
        switch (pkt->type) {
            case 1: { // CONNECT
                // Parse host:port from data (simplified)
                char host[256] = {0};
                int port = 80;
                // In real impl: parse "host:port" string
                strncpy(host, (char*)pkt->data, 255);
                
                int result = handle_connect(pkt->socket_id, host, port);
                
                // Send response
                uint64_t ridx = g_ring->head % RING_SIZE;
                NeonPacket* resp = &g_ring->packets[ridx];
                resp->socket_id = pkt->socket_id;
                resp->type = result == 0 ? 0 : 3; // DATA(ack) or ERROR
                resp->length = 0;
                __sync_fetch_and_add(&g_ring->head, 1);
                break;
            }
            case 2: // CLOSE
                handle_close(pkt->socket_id);
                break;
        }
        
        __sync_fetch_and_add(&g_ring->resp_tail, 1);
    }
}

void poll_sockets() {
    struct pollfd fds[MAX_SOCKETS];
    int nfds = 0;
    
    pthread_mutex_lock(&g_socket_mutex);
    for (int i = 1; i < MAX_SOCKETS; i++) {
        if (g_sockets[i].active && g_sockets[i].fd >= 0) {
            fds[nfds].fd = g_sockets[i].fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }
    }
    pthread_mutex_unlock(&g_socket_mutex);
    
    if (nfds == 0) return;
    
    int ret = poll(fds, nfds, 0); // Non-blocking poll
    if (ret <= 0) return;
    
    // Check which sockets have data
    pthread_mutex_lock(&g_socket_mutex);
    int sock_idx = 0;
    for (int i = 1; i < MAX_SOCKETS && sock_idx < nfds; i++) {
        if (g_sockets[i].active && g_sockets[i].fd >= 0) {
            if (fds[sock_idx].revents & POLLIN) {
                // Read data and push to ring buffer
                uint8_t buffer[1460];
                int len = recv(g_sockets[i].fd, buffer, sizeof(buffer), MSG_DONTWAIT);
                if (len > 0) {
                    uint64_t idx = g_ring->head % RING_SIZE;
                    NeonPacket* pkt = &g_ring->packets[idx];
                    pkt->socket_id = i;
                    pkt->type = 0; // DATA
                    pkt->length = len;
                    memcpy(pkt->data, buffer, len);
                    __sync_fetch_and_add(&g_ring->head, 1);
                } else if (len == 0 || (len < 0 && errno != EAGAIN)) {
                    // Connection closed
                    uint64_t idx = g_ring->head % RING_SIZE;
                    NeonPacket* pkt = &g_ring->packets[idx];
                    pkt->socket_id = i;
                    pkt->type = 2; // CLOSE notification
                    pkt->length = 0;
                    __sync_fetch_and_add(&g_ring->head, 1);
                    g_sockets[i].fd = -1;
                    g_sockets[i].active = false;
                }
            }
            sock_idx++;
        }
    }
    pthread_mutex_unlock(&g_socket_mutex);
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void* worker_thread(void* arg) {
    while (g_running) {
        process_guest_requests();
        poll_sockets();
        usleep(100); // 100us polling interval
    }
    return nullptr;
}

int main() {
    std::cout << "=== NeonBridge Host Daemon ===" << std::endl;
    std::cout << "Initializing shared memory..." << std::endl;

    // Create shared memory region
    int shm_fd = shm_open("/neon_bridge", O_CREAT | O_RDWR, 0666);
    if (shm_fd < 0) {
        perror("shm_open");
        return 1;
    }
    
    if (ftruncate(shm_fd, sizeof(NeonRingBuffer)) < 0) {
        perror("ftruncate");
        return 1;
    }
    
    g_ring = (NeonRingBuffer*)mmap(nullptr, sizeof(NeonRingBuffer), 
                                    PROT_READ | PROT_WRITE, 
                                    MAP_SHARED, shm_fd, 0);
    if (g_ring == MAP_FAILED) {
        perror("mmap");
        return 1;
    }
    
    // Initialize ring buffer
    memset(g_ring, 0, sizeof(NeonRingBuffer));
    g_ring->magic = NEON_MAGIC;
    g_ring->version = 1;
    
    std::cout << "Shared memory ready at /neon_bridge" << std::endl;
    std::cout << "Waiting for TempleOS guest connection..." << std::endl;
    std::cout << "Press Ctrl+C to exit" << std::endl;

    // Start worker thread
    pthread_t worker;
    pthread_create(&worker, nullptr, worker_thread, nullptr);

    // Main loop
    while (g_running) {
        sleep(1);
    }

    pthread_join(worker, nullptr);
    munmap(g_ring, sizeof(NeonRingBuffer));
    close(shm_fd);
    shm_unlink("/neon_bridge");
    
    std::cout << "NeonBridge shutdown complete" << std::endl;
    return 0;
}
