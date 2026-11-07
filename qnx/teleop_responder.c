/* teleop_responder.c — real-time telemetry responder for QNX SDP 8.0.
 *
 * Listens on TCP, reads TwinTeleop binary frames ("TT" codec, see
 * frame.h), validates each frame (magic/version/length/CRC) and echoes
 * the exact bytes back to the sender. The echo is the ack: the host
 * bridge measures round-trip latency from it.
 *
 * Real-time behaviour (QNX Neutrino only, guarded by __QNX__):
 * the serving thread runs at SCHED_FIFO priority 21 and timestamps
 * with ClockCycles() for nanosecond-resolution service-time accounting.
 * On a Linux host build the same logic runs with SCHED_OTHER and
 * clock_gettime(CLOCK_MONOTONIC); the host build exists so the
 * protocol logic is testable without the SDP, and is NOT a claim that
 * the binary has run on QNX.
 *
 * Build (host, for tests):
 *   cc -O2 -Wall -Wextra -std=c99 -Iqnx qnx/teleop_responder.c qnx/frame.c -o responder
 * Build (QNX target, needs SDP 8.0):
 *   qcc -Vgcc_ntox86_64 -O2 -Wall -Wextra -Iqnx qnx/teleop_responder.c qnx/frame.c -o responder
 *
 * Usage: ./responder [port]   (default port 5555)
 */
#define _POSIX_C_SOURCE 199309L /* clock_gettime, sigaction */
#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "frame.h"

#ifdef __QNX__
#include <sys/neutrino.h>
#endif

#define DEFAULT_PORT 5555
#define MAX_CLIENTS 4

static volatile sig_atomic_t stop_requested = 0;

static void on_signal(int sig) {
    (void)sig;
    stop_requested = 1;
}

/* Monotonic timestamp in nanoseconds. */
static uint64_t now_ns(void) {
#ifdef __QNX__
    return ClockCycles() * 1000000000ull /
           (uint64_t)SYSPAGE_ENTRY(qtime)->cycles_per_sec;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
#endif
}

static void set_realtime(void) {
#ifdef __QNX__
    struct sched_param param;
    memset(&param, 0, sizeof param);
    param.sched_priority = 21;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
        perror("pthread_setschedparam (continuing without RT priority)");
    }
#else
    /* Host build: no real-time scheduling available; plain thread. */
    (void)0;
#endif
}

static ssize_t read_exact(int fd, uint8_t *buf, size_t n) {
    size_t got = 0;
    while (got < n) {
        ssize_t r = read(fd, buf + got, n - got);
        if (r == 0) return (ssize_t)got; /* peer closed */
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        got += (size_t)r;
    }
    return (ssize_t)got;
}

static ssize_t write_all(int fd, const uint8_t *buf, size_t n) {
    size_t sent = 0;
    while (sent < n) {
        ssize_t r = write(fd, buf + sent, n - sent);
        if (r <= 0) {
            if (r < 0 && errno == EINTR) continue;
            return -1;
        }
        sent += (size_t)r;
    }
    return (ssize_t)sent;
}

/* Serve one client: read frames, validate, echo. Returns frames served. */
static long serve_client(int fd) {
    long served = 0;
    long rejected = 0;
    uint64_t total_service_ns = 0;
    uint8_t header[TT_HEADER_SIZE];
    uint8_t frame_buf[TT_MAX_FRAME_SIZE];

    for (;;) {
        if (stop_requested) break;
        ssize_t r = read_exact(fd, header, TT_HEADER_SIZE);
        if (r == 0) break; /* clean close */
        if (r < 0 || r != TT_HEADER_SIZE) break;
        if (header[0] != TT_MAGIC_0 || header[1] != TT_MAGIC_1 ||
            header[2] != TT_VERSION) {
            rejected++;
            break; /* protocol desync: drop the connection */
        }
        uint8_t n = header[3];
        if (n < 1 || n > TT_MAX_JOINTS) {
            rejected++;
            break;
        }
        size_t total = tt_encoded_size(n);
        size_t rest = total - TT_HEADER_SIZE;
        memcpy(frame_buf, header, TT_HEADER_SIZE);
        r = read_exact(fd, frame_buf + TT_HEADER_SIZE, rest);
        if (r != (ssize_t)rest) break;
        uint64_t t0 = now_ns();
        tt_frame_t f;
        int rc = tt_decode(frame_buf, total, &f);
        uint64_t t1 = now_ns();
        if (rc != 0) {
            rejected++;
            break;
        }
        total_service_ns += (t1 - t0);
        if (write_all(fd, frame_buf, total) != (ssize_t)total) break;
        served++;
    }
    fprintf(stderr,
            "client done: served=%ld rejected=%ld avg_service_ns=%" PRIu64
            "\n",
            served, rejected,
            served ? total_service_ns / (uint64_t)served : 0);
    return served;
}

int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    if (argc > 1) port = atoi(argv[1]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "invalid port\n");
        return 2;
    }

    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    set_realtime();

    int srv = socket(AF_INET, SOCK_STREAM, 0);
    if (srv < 0) {
        perror("socket");
        return 1;
    }
    int one = 1;
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons((uint16_t)port);
    if (bind(srv, (struct sockaddr *)&addr, sizeof addr) != 0) {
        perror("bind");
        return 1;
    }
    if (listen(srv, MAX_CLIENTS) != 0) {
        perror("listen");
        return 1;
    }
    fprintf(stderr, "teleop_responder listening on 127.0.0.1:%d\n", port);
    while (!stop_requested) {
        int client = accept(srv, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR) continue;
            perror("accept");
            break;
        }
        serve_client(client);
        close(client);
    }
    close(srv);
    return 0;
}
