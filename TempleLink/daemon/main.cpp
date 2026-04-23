#include <iostream>
#include <cstdint>
#include <cstring>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/epoll.h>
#include <csignal>
#include <map>
#include <chrono>
#include <thread>

#include "../include/templelink.h"

// Global state
static TLSharedMemory* g_shm = nullptr;
static int g_shm_fd = -1;
static int g_epoll_fd = -1;
static std::map<int, int> g_socket_map;  // TempleOS sock ID -> Linux FD
static std::atomic<bool> g_running(true);
static const char* SHM_NAME = "/templelink_shm";

void cleanup() {
    g_running = false;
    
    if (g_shm) {
        munmap(g_shm, sizeof(TLSharedMemory));
        g_shm = nullptr;
    }
    
    if (g_shm_fd >= 0) {
        close(g_shm_fd);
        shm_unlink(SHM_NAME);
        g_shm_fd = -1;
    }
    
    if (g_epoll_fd >= 0) {
        close(g_epoll_fd);
        g_epoll_fd = -1;
    }
}

void signal_handler(int sig) {
    std::cout << "Received signal " << sig << ", shutting down..." << std::endl;
    cleanup();
    exit(0);
}

// Lock-free ring buffer operations
inline uint64_t get_next_index(uint64_t idx) {
    return (idx + 1) & TL_RING_MASK;
}

bool has_space(volatile uint64_t& head, volatile uint64_t& tail) {
    return get_next_index(head) != tail;
}

bool has_data(volatile uint64_t& head, volatile uint64_t& tail) {
    return head != tail;
}

// Process socket creation request
int process_socket(TLRequest* req, TLResponse* resp) {
    int domain = (req->domain == TL_AF_INET) ? AF_INET : 
                 (req->domain == TL_AF_INET6) ? AF_INET6 : AF_UNIX;
    int type = (req->type == TL_SOCK_STREAM) ? SOCK_STREAM :
               (req->type == TL_SOCK_DGRAM) ? SOCK_DGRAM : SOCK_RAW;
    
    int fd = socket(domain, type, req->protocol);
    if (fd < 0) {
        resp->result = -1;
        resp->errno_val = errno;
        return -1;
    }
    
    // Set non-blocking by default
    fcntl(fd, F_SETFL, O_NONBLOCK);
    
    // Assign TempleOS socket ID
    static int next_sock_id = 1000;
    int sock_id = next_sock_id++;
    g_socket_map[sock_id] = fd;
    
    resp->socket_id = sock_id;
    resp->result = sock_id;
    resp->errno_val = 0;
    
    std::cout << "Created socket: TempleID=" << sock_id << " LinuxFD=" << fd << std::endl;
    return 0;
}

// Process connect request
int process_connect(TLRequest* req, TLResponse* resp) {
    auto it = g_socket_map.find(req->socket_id);
    if (it == g_socket_map.end()) {
        resp->result = -1;
        resp->errno_val = EBADF;
        return -1;
    }
    
    int fd = it->second;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    
    // Parse address from request payload (format: "IP:PORT")
    char ip_str[64], port_str[16];
    sscanf((char*)req->payload, "%63[^:]:%15s", ip_str, port_str);
    
    inet_pton(AF_INET, ip_str, &addr.sin_addr);
    addr.sin_port = htons(atoi(port_str));
    
    int result = connect(fd, (struct sockaddr*)&addr, sizeof(addr));
    if (result < 0 && errno != EINPROGRESS) {
        resp->result = -1;
        resp->errno_val = errno;
        return -1;
    }
    
    resp->result = 0;
    resp->errno_val = 0;
    return 0;
}

// Process send request
int process_send(TLRequest* req, TLResponse* resp) {
    auto it = g_socket_map.find(req->socket_id);
    if (it == g_socket_map.end()) {
        resp->result = -1;
        resp->errno_val = EBADF;
        return -1;
    }
    
    int fd = it->second;
    ssize_t sent = send(fd, req->payload, req->payload_len, 0);
    
    if (sent < 0) {
        resp->result = -1;
        resp->errno_val = errno;
        return -1;
    }
    
    resp->result = sent;
    resp->errno_val = 0;
    return 0;
}

// Process recv request
int process_recv(TLRequest* req, TLResponse* resp) {
    auto it = g_socket_map.find(req->socket_id);
    if (it == g_socket_map.end()) {
        resp->result = -1;
        resp->errno_val = EBADF;
        return -1;
    }
    
    int fd = it->second;
    ssize_t received = recv(fd, resp->payload, TL_MAX_PAYLOAD - 1, 0);
    
    if (received < 0) {
        resp->result = -1;
        resp->errno_val = errno;
        return -1;
    }
    
    resp->payload_len = received;
    resp->result = received;
    resp->errno_val = 0;
    return 0;
}

// Process close request
int process_close(TLRequest* req, TLResponse* resp) {
    auto it = g_socket_map.find(req->socket_id);
    if (it == g_socket_map.end()) {
        resp->result = -1;
        resp->errno_val = EBADF;
        return -1;
    }
    
    int fd = it->second;
    close(fd);
    g_socket_map.erase(it);
    
    resp->result = 0;
    resp->errno_val = 0;
    std::cout << "Closed socket: TempleID=" << req->socket_id << std::endl;
    return 0;
}

// Main request processor
void process_request(TLRequest* req) {
    TLResponse resp;
    memset(&resp, 0, sizeof(resp));
    resp.id = req->id;
    
    switch (req->op_code) {
        case TL_OP_SOCKET:
            process_socket(req, &resp);
            break;
        case TL_OP_CONNECT:
            process_connect(req, &resp);
            break;
        case TL_OP_SEND:
            process_send(req, &resp);
            break;
        case TL_OP_RECV:
            process_recv(req, &resp);
            break;
        case TL_OP_CLOSE:
            process_close(req, &resp);
            break;
        default:
            resp.result = -1;
            resp.errno_val = EINVAL;
            break;
    }
    
    // Write response to ring buffer
    uint64_t head = g_shm->header.response_head;
    while (!has_space(g_shm->header.response_head, g_shm->header.response_tail)) {
        // Wait for space (spin briefly then yield)
        std::this_thread::yield();
    }
    
    g_shm->responses[head].resp = resp;
    std::atomic_thread_fence(std::memory_order_release);
    g_shm->header.response_head = get_next_index(head);
    g_shm->header.stats_responses++;
}

// Main event loop
void main_loop() {
    std::cout << "TempleLink daemon started. PID: " << getpid() << std::endl;
    std::cout << "Waiting for TempleOS connections..." << std::endl;
    
    // Initialize shared memory header
    g_shm->header.version = TEMPLELINK_VERSION;
    g_shm->header.daemon_pid = getpid();
    g_shm->header.request_head = 0;
    g_shm->header.request_tail = 0;
    g_shm->header.response_head = 0;
    g_shm->header.response_tail = 0;
    
    std::atomic_thread_fence(std::memory_order_release);
    
    while (g_running) {
        // Check for new requests
        uint64_t tail = g_shm->header.request_tail;
        uint64_t head = g_shm->header.request_head;
        
        if (has_data(head, tail)) {
            TLRequest* req = &g_shm->requests[tail].req;
            std::atomic_thread_fence(std::memory_order_acquire);
            
            process_request(req);
            
            // Advance tail
            g_shm->header.request_tail = get_next_index(tail);
            g_shm->header.stats_requests++;
        } else {
            // No requests, sleep briefly to save CPU
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    std::cout << "TempleLink Daemon v1.0" << std::endl;
    std::cout << "Symbiotic Linux-TempleOS Networking Bridge" << std::endl;
    
    // Setup signal handlers
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Create shared memory
    g_shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR, 0666);
    if (g_shm_fd < 0) {
        std::cerr << "Failed to create shared memory: " << strerror(errno) << std::endl;
        return 1;
    }
    
    if (ftruncate(g_shm_fd, sizeof(TLSharedMemory)) < 0) {
        std::cerr << "Failed to resize shared memory: " << strerror(errno) << std::endl;
        close(g_shm_fd);
        return 1;
    }
    
    g_shm = (TLSharedMemory*)mmap(nullptr, sizeof(TLSharedMemory),
                                   PROT_READ | PROT_WRITE,
                                   MAP_SHARED, g_shm_fd, 0);
    if (g_shm == MAP_FAILED) {
        std::cerr << "Failed to map shared memory: " << strerror(errno) << std::endl;
        close(g_shm_fd);
        return 1;
    }
    
    memset(g_shm, 0, sizeof(TLSharedMemory));
    
    // Create epoll for future interrupt handling
    g_epoll_fd = epoll_create1(0);
    
    atexit(cleanup);
    
    main_loop();
    
    return 0;
}
