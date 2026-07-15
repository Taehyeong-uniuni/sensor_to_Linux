/*
 * G005: mock TCP 8141 streaming server. Reproduces ONLY behavior directly
 * confirmed by reading the pinned streaming_server_v2 source (commit
 * 39a6f49343e38ff8b62bb3d1ab7233065d593d4a) - it does not invent server
 * behavior beyond what was actually observed there:
 *   - PIRIN ('I') is echoed back as Command='I' (NOT 'R'), empty body.
 *   - PIROUT ('O') gets no response at all - the server just closes.
 *   - Data commands (S/Z, V/V/Z) get a Command='R' response whose body
 *     is the server's actual non-strict-JSON wire format: unquoted key,
 *     e.g. "{result:4}" / "{result:7}" - not "{"result":4}".
 *   - Inbound CRC is never validated server-side (DeviceDecoder.java just
 *     stores the field) - this mock does not reject on bad CRC either.
 *   - Declared length is bounds-checked and rejected by closing the
 *     connection (DeviceDecoder.java MAX_DATA_LENGTH check).
 *
 * This is a TEST DOUBLE only: it is not linked into any production
 * target (see tools/mock_streaming_server/CMakeLists.txt - a standalone
 * executable with no callers anywhere under src), and it never makes any
 * real network connection of its own (it only accept()s local loopback
 * connections the caller/test directs at it).
 *
 * Usage: mock_streaming_server --fixture=<name> [--port=N]
 * Prints "PORT <n>" on its own line as soon as it is listening, so a
 * caller that requested an ephemeral port (the default, --port=0) can
 * discover which one was assigned.
 */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include "savvy/protocol/packet_codec.h"
#include "savvy/protocol/stream_parser.h"

static const uint8_t SERIAL[SAVVY_PACKET_SERIAL_LEN] = {'0','0','0','0','0','0','0','0','0','0','0','0','0','1'};

static int make_listener(uint16_t requested_port, uint16_t *out_port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return -1;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(requested_port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 8) != 0) {
        close(fd);
        return -1;
    }
    socklen_t alen = sizeof(addr);
    getsockname(fd, (struct sockaddr *)&addr, &alen);
    *out_port = ntohs(addr.sin_port);
    return fd;
}

static bool send_all_bytes(int fd, const uint8_t *data, size_t data_len)
{
    size_t off = 0;
    while (off < data_len) {
        ssize_t n = send(fd, data + off, data_len - off, 0);
        if (n > 0) {
            off += (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR) {
            continue;
        }
        return false;
    }
    return true;
}

static size_t encode_response(uint8_t start, uint8_t command,
                              const uint8_t *data, size_t data_len,
                              uint8_t *buf, size_t buf_cap)
{
    size_t written = 0;
    if (savvy_packet_encode(start, command, 0, 0, SERIAL, sizeof(SERIAL),
                            data, data_len, buf, buf_cap, &written) != SAVVY_OK) {
        return 0;
    }
    return written;
}

static void send_packet(int fd, uint8_t start, uint8_t command, const uint8_t *data, size_t data_len)
{
    uint8_t buf[262144];
    size_t written = encode_response(start, command, data, data_len, buf, sizeof(buf));
    if (written > 0) {
        (void)send_all_bytes(fd, buf, written);
    }
}

static void send_split_response(int fd, uint8_t start, size_t first_chunk_len)
{
    static const uint8_t BODY[] = "{result:4}";
    uint8_t buf[512];
    size_t written = encode_response(start, (uint8_t)'R', BODY, sizeof(BODY) - 1u,
                                     buf, sizeof(buf));
    if (written == 0 || first_chunk_len == 0 || first_chunk_len >= written) {
        return;
    }
    if (send_all_bytes(fd, buf, first_chunk_len)) {
        usleep(20 * 1000);
        (void)send_all_bytes(fd, buf + first_chunk_len, written - first_chunk_len);
    }
}

/* Reads and decodes exactly one packet, or returns false on framing
 * error / disconnect. `partial_style` controls how the read is chunked,
 * for the fragmentation fixtures. */
typedef enum { READ_NORMAL, READ_ONE_BYTE_AT_A_TIME, READ_HEADER_THEN_BODY_SEPARATELY } read_style_t;

static bool read_one_packet(int fd, uint8_t *scratch, size_t scratch_cap, read_style_t style,
                             savvy_packet_header_t *out_hdr, const uint8_t **out_data, size_t *out_data_len)
{
    savvy_stream_parser_t parser;
    savvy_stream_parser_init(&parser, scratch, scratch_cap);

    size_t chunk_size = (style == READ_ONE_BYTE_AT_A_TIME) ? 1
                       : (style == READ_HEADER_THEN_BODY_SEPARATELY) ? 5
                       : 65536;
    uint8_t buf[65536];
    for (;;) {
        ssize_t n = recv(fd, buf, chunk_size, 0);
        if (n <= 0) {
            return false;
        }
        if (savvy_stream_parser_push(&parser, buf, (size_t)n) != SAVVY_OK) {
            return false;
        }
        savvy_stream_result_t r = savvy_stream_parser_try_extract(&parser, out_hdr, out_data, out_data_len);
        if (r == SAVVY_STREAM_PACKET_READY) {
            return true;
        }
        if (r == SAVVY_STREAM_ERROR) {
            return false;
        }
        if (style == READ_HEADER_THEN_BODY_SEPARATELY) {
            chunk_size = 65536; /* first chunk was just the 5-byte fragment; read the rest normally */
        }
    }
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);

    const char *fixture = NULL;
    uint16_t requested_port = 0;
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--fixture=", 10) == 0) {
            fixture = argv[i] + 10;
        } else if (strncmp(argv[i], "--port=", 7) == 0) {
            requested_port = (uint16_t)atoi(argv[i] + 7);
        }
    }
    if (fixture == NULL) {
        fprintf(stderr, "usage: %s --fixture=<name> [--port=N]\n", argv[0]);
        fprintf(stderr, "fixtures: s-i-echo v-i-echo s-r-normal s-r-danger v-r-normal v-r-danger "
                        "s-o-no-response v-o-no-response partial-header split-body coalesced "
                        "invalid-crc length-overflow delay-then-respond delay-exceeds-timeout "
                        "stale-response-after-timeout disconnect-before-header disconnect-during-body "
                        "stream-only-failure voice-only-failure slow-processing\n");
        return 2;
    }

    uint16_t port = 0;
    int listen_fd = make_listener(requested_port, &port);
    if (listen_fd < 0) {
        fprintf(stderr, "failed to create listener: %s\n", strerror(errno));
        return 1;
    }
    printf("PORT %u\n", (unsigned)port);
    fflush(stdout);

    static uint8_t scratch[1 << 21]; /* 2MB - large enough for the largest pinned Stream payload */

    struct sockaddr_in peer;
    socklen_t plen = sizeof(peer);
    int fd = accept(listen_fd, (struct sockaddr *)&peer, &plen);
    if (fd < 0) {
        fprintf(stderr, "accept failed: %s\n", strerror(errno));
        return 1;
    }

    if (strcmp(fixture, "s-i-echo") == 0 || strcmp(fixture, "v-i-echo") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            send_packet(fd, hdr.start, (uint8_t)'I', NULL, 0);
        }
    } else if (strcmp(fixture, "s-r-normal") == 0 || strcmp(fixture, "v-r-normal") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            const char *body = "{result:4}"; /* confirmed real-server unquoted-key format */
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)body, strlen(body));
        }
    } else if (strcmp(fixture, "s-r-danger") == 0 || strcmp(fixture, "v-r-danger") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            const char *body = strcmp(fixture, "v-r-danger") == 0 ? "{result:7}" : "{result:6}";
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)body, strlen(body));
        }
    } else if (strcmp(fixture, "s-o-no-response") == 0 || strcmp(fixture, "v-o-no-response") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        /* Confirmed real behavior: read the PIROUT, send nothing, just close. */
        read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len);
    } else if (strcmp(fixture, "partial-header") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            /* Split inside the 26-byte RESPONSE header. */
            send_split_response(fd, hdr.start, 5u);
        }
    } else if (strcmp(fixture, "split-body") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            /* Send the full header plus three response-body bytes, then
             * the remainder, so fragmentation is response-side. */
            send_split_response(fd, hdr.start, (size_t)SAVVY_PACKET_HEADER_LEN + 3u);
        }
    } else if (strcmp(fixture, "coalesced") == 0) {
        /* Reads and answers TWO requests, but the two responses below are
         * written back-to-back in a single send() so the client must
         * distinguish them via stream_parser rather than assuming one
         * read() == one packet. */
        savvy_packet_header_t hdr1, hdr2;
        const uint8_t *d1 = NULL, *d2 = NULL;
        size_t l1 = 0, l2 = 0;
        savvy_stream_parser_t parser;
        savvy_stream_parser_init(&parser, scratch, sizeof(scratch));
        uint8_t buf[65536];
        savvy_stream_result_t r = SAVVY_STREAM_NEED_MORE_DATA;
        while (r == SAVVY_STREAM_NEED_MORE_DATA) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                close(fd);
                close(listen_fd);
                return 0;
            }
            savvy_stream_parser_push(&parser, buf, (size_t)n);
            r = savvy_stream_parser_try_extract(&parser, &hdr1, &d1, &l1);
        }
        r = savvy_stream_parser_try_extract(&parser, &hdr2, &d2, &l2);
        while (r == SAVVY_STREAM_NEED_MORE_DATA) {
            ssize_t n = recv(fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break;
            }
            savvy_stream_parser_push(&parser, buf, (size_t)n);
            r = savvy_stream_parser_try_extract(&parser, &hdr2, &d2, &l2);
        }
        uint8_t coalesced[512];
        size_t off = 0, w = 0;
        savvy_packet_encode(hdr1.start, (uint8_t)'R', 0, 0, SERIAL, sizeof(SERIAL), (const uint8_t *)"{result:4}", 10, coalesced + off, sizeof(coalesced) - off, &w);
        off += w;
        if (r == SAVVY_STREAM_PACKET_READY) {
            savvy_packet_encode(hdr2.start, (uint8_t)'R', 0, 0, SERIAL, sizeof(SERIAL), (const uint8_t *)"{result:4}", 10, coalesced + off, sizeof(coalesced) - off, &w);
            off += w;
        }
        send(fd, coalesced, off, 0); /* both responses in ONE send() call - the coalescing under test */
    } else if (strcmp(fixture, "invalid-crc") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            uint8_t buf[512];
            size_t written = 0;
            const char *body = "{result:4}";
            savvy_packet_encode(hdr.start, (uint8_t)'R', 0, 0, SERIAL, sizeof(SERIAL), (const uint8_t *)body, strlen(body), buf, sizeof(buf), &written);
            buf[22] ^= 0xFF; /* corrupt one byte of the CRC32 field (offset 22, per the fixed 26-byte header) */
            send(fd, buf, written, 0);
        }
    } else if (strcmp(fixture, "length-overflow") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len);
        /* Hand-crafted header declaring an absurd length, matching the
         * real DeviceDecoder's MAX_DATA_LENGTH-style rejection scenario -
         * a conforming client must reject this (SAVVY_STREAM_ERROR) and
         * close, not read past its buffer. */
        uint8_t bogus[SAVVY_PACKET_HEADER_LEN];
        bogus[0] = hdr.start;
        bogus[1] = (uint8_t)'R';
        bogus[2] = 0;
        bogus[3] = 0;
        bogus[4] = 0x7F; bogus[5] = 0xFF; bogus[6] = 0xFF; bogus[7] = 0xFF; /* length ~= 2^31-1 */
        memcpy(bogus + 8, SERIAL, sizeof(SERIAL));
        bogus[22] = 0; bogus[23] = 0; bogus[24] = 0; bogus[25] = 0;
        send(fd, bogus, sizeof(bogus), 0);
    } else if (strcmp(fixture, "delay-then-respond") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            usleep(500 * 1000); /* well within the pinned 3000ms response timeout */
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)"{result:4}", 10);
        }
    } else if (strcmp(fixture, "delay-exceeds-timeout") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            usleep(600 * 1000); /* paired CTest uses a bounded 250ms response timeout */
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)"{result:4}", 10);
        }
    } else if (strcmp(fixture, "stale-response-after-timeout") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            usleep(600 * 1000); /* client times out at 250ms and closes before this late response */
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)"{result:4}", 10); /* write to an already-closed peer - expected to fail/be ignored */
        }
    } else if (strcmp(fixture, "disconnect-before-header") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        /* Consume the request, then close without sending any RESPONSE
         * header bytes. */
        (void)read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len);
    } else if (strcmp(fixture, "disconnect-during-body") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            uint8_t response[512];
            size_t response_len = encode_response(hdr.start, (uint8_t)'R',
                                                  (const uint8_t *)"{result:4}", 10,
                                                  response, sizeof(response));
            if (response_len > (size_t)SAVVY_PACKET_HEADER_LEN + 4u) {
                /* Full response header plus only four body bytes, then
                 * close before the declared body is complete. */
                (void)send_all_bytes(fd, response, (size_t)SAVVY_PACKET_HEADER_LEN + 4u);
            }
        }
    } else if (strcmp(fixture, "stream-only-failure") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        /* Consume the Stream request and close without a response. The
         * paired test runs a separate healthy Voice fixture concurrently. */
        (void)read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len);
    } else if (strcmp(fixture, "voice-only-failure") == 0) {
        /* Same shape as stream-only-failure, named separately only for
         * fixture-selection clarity at the call site. */
    } else if (strcmp(fixture, "slow-processing") == 0) {
        savvy_packet_header_t hdr;
        const uint8_t *data = NULL;
        size_t data_len = 0;
        if (read_one_packet(fd, scratch, sizeof(scratch), READ_NORMAL, &hdr, &data, &data_len)) {
            usleep(200 * 1000); /* deliberately slow, to help a caller build up queue-overflow conditions upstream */
            send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)"{result:4}", 10);
        }
    } else {
        fprintf(stderr, "unknown fixture: %s\n", fixture);
        close(fd);
        close(listen_fd);
        return 2;
    }

    close(fd);
    close(listen_fd);
    return 0;
}
