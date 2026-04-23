#ifndef TEMPLELINK_H
#define TEMPLELINK_H

#include <stdint.h>
#include <stddef.h>

// Protocol version for compatibility checking
#define TEMPLELINK_VERSION 1

// Operation codes for ring buffer commands
typedef enum {
    TL_OP_NONE = 0,
    TL_OP_SOCKET,
    TL_OP_CONNECT,
    TL_OP_BIND,
    TL_OP_LISTEN,
    TL_OP_ACCEPT,
    TL_OP_SEND,
    TL_OP_RECV,
    TL_OP_CLOSE,
    TL_OP_GETSOCKOPT,
    TL_OP_SETSOCKOPT,
    TL_OP_SELECT,
    TL_OP_POLL
} TLOpCode;

// Socket domain constants (matching Linux)
#define TL_AF_INET      2
#define TL_AF_INET6    10
#define TL_AF_UNIX      1

// Socket type constants
#define TL_SOCK_STREAM  1
#define TL_SOCK_DGRAM   2
#define TL_SOCK_RAW     3

// Ring buffer configuration
#define TL_RING_SIZE    4096
#define TL_RING_MASK    (TL_RING_SIZE - 1)
#define TL_MAX_PAYLOAD  8192

// Shared memory header structure
typedef struct {
    uint32_t version;
    uint32_t flags;
    uint64_t daemon_pid;
    uint64_t templeos_vm_id;
    volatile uint64_t request_head;
    volatile uint64_t request_tail;
    volatile uint64_t response_head;
    volatile uint64_t response_tail;
    uint64_t stats_requests;
    uint64_t stats_responses;
    uint64_t stats_errors;
    uint8_t  reserved[40];
} TLSharedHeader;

// Request structure in ring buffer
typedef struct {
    uint64_t id;
    uint32_t op_code;
    uint32_t socket_id;
    int32_t  domain;
    int32_t  type;
    int32_t  protocol;
    uint32_t payload_len;
    uint8_t  addr[64];  // sockaddr_storage equivalent
    uint8_t  payload[TL_MAX_PAYLOAD];
    uint64_t timestamp_ns;
} TLRequest;

// Response structure in ring buffer
typedef struct {
    uint64_t id;
    int32_t  result;
    int32_t  errno_val;
    uint32_t socket_id;
    uint32_t payload_len;
    uint8_t  payload[TL_MAX_PAYLOAD];
    uint64_t timestamp_ns;
} TLResponse;

// Ring buffer entry (unions request/response)
typedef struct {
    union {
        TLRequest  req;
        TLResponse resp;
    };
    uint8_t padding[64];  // Cache line alignment
} TLRingEntry;

// Complete shared memory layout
typedef struct {
    TLSharedHeader header;
    TLRingEntry    requests[TL_RING_SIZE];
    TLRingEntry    responses[TL_RING_SIZE];
} TLSharedMemory;

// HolyC API function declarations (for reference)
// These would be implemented in TempleLink.HC:
//
// U0 TL_Init();
// I64 TL_Socket(I32 domain, I32 type, I32 protocol);
// I64 TL_Connect(I64 sock, U8 *addr, I32 port);
// I64 TL_Bind(I64 sock, U8 *addr, I32 port);
// I64 TL_Listen(I64 sock, I32 backlog);
// I64 TL_Accept(I64 sock, U8 *addr, I32 *port);
// I64 TL_Send(I64 sock, U8 *buf, I64 len, I32 flags);
// I64 TL_Recv(I64 sock, U8 *buf, I64 len, I32 flags);
// I64 TL_Close(I64 sock);
// void TL_Shutdown();

// Error codes
#define TL_SUCCESS          0
#define TL_ERR_INVALID     -1
#define TL_ERR_NOMEM       -2
#define TL_ERR_TIMEOUT     -3
#define TL_ERR_CONNREFUSED -4
#define TL_ERR_NETUNREACH  -5
#define TL_ERR_ALREADY     -6

// Flags
#define TL_FLAG_NONBLOCK   0x01
#define TL_FLAG_CLOEXEC    0x02

#endif // TEMPLELINK_H
