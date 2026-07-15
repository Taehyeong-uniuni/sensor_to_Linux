/* SNS-STR-001 (Stream full I->S/Z->O flow, lazy connect, timeouts,
 * serial send->receive, O not awaiting a response) and SNS-STR-003
 * (Voice full I->V/Z->O flow, fully independent worker/socket/queue/state
 * from Stream, O not awaiting a response). Exercises the real
 * sensor_stream_session_t API end-to-end against a minimal mock server
 * that reproduces the CONFIRMED real streaming_server_v2 wire behavior:
 * PIRIN echoes Command='I' (not 'R') with an empty body; data commands
 * get a Command='R' response with an unquoted-key `{result:N}` body
 * (the real server's actual, non-strict-JSON wire format); PIROUT gets
 * no response at all, just a connection close. */
#define _DEFAULT_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sensor_stream/session.h"
#include "savvy/protocol/packet_codec.h"
#include "savvy/protocol/stream_parser.h"

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static const uint8_t SERIAL[SAVVY_PACKET_SERIAL_LEN] = {'0','0','0','0','0','0','0','0','0','0','0','0','0','1'};

/* ---- mock server: reproduces the confirmed real-server response shapes ---- */
typedef struct {
    int listen_fd;
    uint16_t port;
    const char *danger_result_body; /* e.g. "{result:7}" (unquoted, real-server format) or NULL to always answer normal */
    int expected_connections;
    int seen_stream_or_voice_data_command; /* out: last non-control Command byte seen, for assertions */
    uint8_t seen_device;
    uint8_t seen_config;
    bool seen_wav_magic; /* out: true if a body started with "RIFF" (proves WAV-wrap happened before this arrived) */
} mock_server_t;

static bool make_listener(mock_server_t *m)
{
    m->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (m->listen_fd < 0) {
        return false;
    }
    int one = 1;
    setsockopt(m->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(m->listen_fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(m->listen_fd);
        return false;
    }
    if (listen(m->listen_fd, 8) != 0) {
        close(m->listen_fd);
        return false;
    }
    socklen_t alen = sizeof(addr);
    getsockname(m->listen_fd, (struct sockaddr *)&addr, &alen);
    m->port = ntohs(addr.sin_port);
    return true;
}

static void send_packet(int fd, uint8_t start, uint8_t command, const uint8_t *data, size_t data_len)
{
    uint8_t buf[8192];
    size_t written = 0;
    savvy_packet_encode(start, command, 0, 0, SERIAL, sizeof(SERIAL), data, data_len, buf, sizeof(buf), &written);
    send(fd, buf, written, 0);
}

/* Handles connections one at a time (matching the real single-in-flight
 * request/response contract): for each connection, read one packet,
 * inspect Start/Command, and respond per the confirmed real behavior. */
static void *mock_server_thread(void *arg)
{
    mock_server_t *m = (mock_server_t *)arg;
    for (int i = 0; i < m->expected_connections; i++) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof(peer);
        int fd = accept(m->listen_fd, (struct sockaddr *)&peer, &plen);
        if (fd < 0) {
            continue;
        }

        /* Heap-allocated: pthread's default secondary-thread stack (as
         * small as 512KB on macOS) cannot hold a 1MB local array - an
         * earlier version of this test stack-allocated this and crashed
         * with a bus error/stack overflow the moment this function was
         * entered. Test packets here are at most a few KB, but size
         * generously since this is just a reassembly scratch buffer. */
        uint8_t *recvbuf = (uint8_t *)malloc(1 << 20);
        savvy_stream_parser_t parser;
        savvy_stream_parser_init(&parser, recvbuf, 1 << 20);

        /* The pinned client reuses ONE connection across a whole
         * PIRIN -> data(s) -> PIROUT sequence (see the comment at each
         * test's expected_connections) - so this loop keeps handling
         * request/response exchanges on the SAME accepted connection
         * until PIROUT (which the confirmed real server handles by
         * closing with no response) or the peer disconnects. */
        bool keep_going = true;
        uint8_t chunk[65536];
        while (keep_going) {
            savvy_packet_header_t hdr;
            const uint8_t *data = NULL;
            size_t data_len = 0;
            savvy_stream_result_t r = SAVVY_STREAM_NEED_MORE_DATA;
            while (r == SAVVY_STREAM_NEED_MORE_DATA) {
                ssize_t n = recv(fd, chunk, sizeof(chunk), 0);
                if (n <= 0) {
                    keep_going = false;
                    break;
                }
                savvy_stream_parser_push(&parser, chunk, (size_t)n);
                r = savvy_stream_parser_try_extract(&parser, &hdr, &data, &data_len);
            }
            if (!keep_going) {
                break;
            }
            if (r != SAVVY_STREAM_PACKET_READY) {
                break; /* framing error - unrecoverable, matching the real decoder's ctx.close() */
            }

            if (hdr.command == (uint8_t)'I') {
                /* Confirmed real behavior: echo Command='I', empty body. */
                send_packet(fd, hdr.start, (uint8_t)'I', NULL, 0);
            } else if (hdr.command == (uint8_t)'O') {
                /* Confirmed real behavior: no response at all, just close. */
                keep_going = false;
            } else {
                m->seen_stream_or_voice_data_command = hdr.command;
                m->seen_device = hdr.device;
                m->seen_config = hdr.config;
                m->seen_wav_magic = (data_len >= 4 && memcmp(data, "RIFF", 4) == 0);
                const char *body = m->danger_result_body != NULL ? m->danger_result_body : "{result:4}";
                send_packet(fd, hdr.start, (uint8_t)'R', (const uint8_t *)body, strlen(body));
            }
        }
        close(fd);
        free(recvbuf);
    }
    return NULL;
}

typedef struct { int calls; bool last_ok; uint8_t last_command; } sent_capture_t;
static void capture_sent(bool ok, uint8_t response_command, void *ctx)
{
    sent_capture_t *c = (sent_capture_t *)ctx;
    c->calls++;
    c->last_ok = ok;
    c->last_command = response_command;
}

typedef struct { int calls; uint8_t last_start; } alert_capture_t;
static void capture_alert(uint8_t channel_start, void *ctx)
{
    alert_capture_t *c = (alert_capture_t *)ctx;
    c->calls++;
    c->last_start = channel_start;
}

/* ---- SNS-STR-001: Stream full I -> S(raw) -> O flow ---- */
static void test_001(void)
{
    mock_server_t srv = {0};
    CHECK(make_listener(&srv), "create mock server for stream flow");
    /* Exactly ONE connection for the whole I->S->O sequence: the pinned
     * Android contract (and this implementation) connects once (lazily,
     * on the first request) and REUSES that same connection across
     * subsequent requests until an error/timeout or a PIROUT close - it
     * does not open a fresh socket per individual command. */
    srv.expected_connections = 1;
    pthread_t th;
    pthread_create(&th, NULL, mock_server_thread, &srv);

    sensor_stream_config_t cfg = {0};
    cfg.server_ip = "127.0.0.1";
    cfg.server_port = srv.port;
    cfg.compress = 0;
    cfg.danger_count_threshold = 4;
    cfg.device_serial = SERIAL;
    cfg.max_payload_size = 65536;

    alert_capture_t alert = {0};
    sensor_stream_session_t *s = NULL;
    CHECK(sensor_stream_session_create(&s, SENSOR_STREAM_ROLE_STREAM, &cfg, capture_alert, &alert) == SAVVY_OK, "create stream session");
    CHECK(sensor_stream_session_start(s) == SAVVY_OK, "start stream session");
    CHECK(sensor_stream_session_is_connected(s) == false, "not connected before first send (lazy connect)");

    sent_capture_t c1 = {0};
    sensor_stream_session_send_pirin(s, capture_sent, &c1);
    usleep(300 * 1000);
    CHECK(c1.calls == 1 && c1.last_ok, "S/I completes ok (real-server 'I' echo recognized, not misread as a failure)");

    uint8_t frame[256];
    memset(frame, 0x42, sizeof(frame));
    sent_capture_t c2 = {0};
    sensor_stream_session_send_data(s, frame, sizeof(frame), 0, capture_sent, &c2);
    usleep(300 * 1000);
    CHECK(c2.calls == 1 && c2.last_ok, "S/S (raw, compress=0) completes ok with a Command='R' response");
    CHECK(srv.seen_stream_or_voice_data_command == (uint8_t)'S', "server received Command='S' (raw STREAM, not STREAM_BZIP) since compress=0");
    CHECK(alert.calls == 0, "no alert fires for a normal (result=4) response");

    sent_capture_t c3 = {0};
    sensor_stream_session_send_pirout(s, capture_sent, &c3);
    usleep(300 * 1000);
    CHECK(c3.calls == 1 && c3.last_ok, "S/O completes ok without waiting for any response");
    CHECK(c3.last_command == 0, "S/O reports response_command=0 - it never interprets a response at all");

    sensor_stream_session_destroy(s);
    pthread_join(th, NULL);
    close(srv.listen_fd);
}

/* ---- SNS-STR-003: Voice full I -> V/Z(compressed) -> O flow, decibel packing, WAV wrap, independence from Stream ---- */
static void test_003(void)
{
    mock_server_t srv = {0};
    CHECK(make_listener(&srv), "create mock server for voice flow");
    srv.expected_connections = 1; /* one persistent connection for I->Z->O, see test_001's comment */
    srv.danger_result_body = "{result:7}"; /* real-server unquoted-key danger response */
    pthread_t th;
    pthread_create(&th, NULL, mock_server_thread, &srv);

    sensor_stream_config_t cfg = {0};
    cfg.server_ip = "127.0.0.1";
    cfg.server_port = srv.port;
    cfg.compress = 1; /* force STREAM_BZIP path */
    cfg.danger_count_threshold = 4; /* irrelevant for Voice - never counts */
    cfg.device_serial = SERIAL;
    cfg.max_payload_size = 65536;

    alert_capture_t alert = {0};
    sensor_stream_session_t *s = NULL;
    CHECK(sensor_stream_session_create(&s, SENSOR_STREAM_ROLE_VOICE, &cfg, capture_alert, &alert) == SAVVY_OK, "create voice session");
    sensor_stream_session_start(s);

    sent_capture_t c1 = {0};
    sensor_stream_session_send_pirin(s, capture_sent, &c1);
    usleep(300 * 1000);
    CHECK(c1.calls == 1 && c1.last_ok, "V/I completes ok");

    uint8_t pcm[512];
    memset(pcm, 0x7f, sizeof(pcm));
    int16_t mic_value = -1234;
    sent_capture_t c2 = {0};
    sensor_stream_session_send_data(s, pcm, sizeof(pcm), mic_value, capture_sent, &c2);
    usleep(300 * 1000);
    CHECK(c2.calls == 1 && c2.last_ok, "V/Z (compressed) completes ok with a Command='R' response");
    CHECK(srv.seen_stream_or_voice_data_command == (uint8_t)'Z', "server received Command='Z' (STREAM_BZIP) since compress=1");

    uint16_t expected_uv = (uint16_t)mic_value;
    uint8_t expected_device = (uint8_t)(expected_uv >> 8);
    uint8_t expected_config = (uint8_t)(expected_uv & 0xFFu);
    CHECK(srv.seen_device == expected_device && srv.seen_config == expected_config,
          "mic_value is packed into Device/Config bytes exactly as the pinned Android sender does (device=uv>>8, config=uv&0xFF)");

    CHECK(alert.calls == 1 && alert.last_start == (uint8_t)'V', "voice alerts immediately on the first non-normal (unquoted {result:7}) response, no threshold");

    sent_capture_t c3 = {0};
    sensor_stream_session_send_pirout(s, capture_sent, &c3);
    usleep(300 * 1000);
    CHECK(c3.calls == 1 && c3.last_ok, "V/O completes ok without waiting for any response");

    sensor_stream_session_destroy(s);
    pthread_join(th, NULL);
    close(srv.listen_fd);
}

/* WAV-wrap verification isolated from compression, so the mock server can
 * see the plaintext RIFF header (compress=0 -> body reaches the wire
 * unmodified after WAV wrapping). */
static void test_003_wav(void)
{
    mock_server_t srv = {0};
    CHECK(make_listener(&srv), "create mock server for wav-wrap check");
    srv.expected_connections = 1;
    pthread_t th;
    pthread_create(&th, NULL, mock_server_thread, &srv);

    sensor_stream_config_t cfg = {0};
    cfg.server_ip = "127.0.0.1";
    cfg.server_port = srv.port;
    cfg.compress = 0;
    cfg.danger_count_threshold = 4;
    cfg.device_serial = SERIAL;
    cfg.max_payload_size = 65536;

    sensor_stream_session_t *s = NULL;
    sensor_stream_session_create(&s, SENSOR_STREAM_ROLE_VOICE, &cfg, NULL, NULL);
    sensor_stream_session_start(s);

    uint8_t pcm[128];
    memset(pcm, 0x11, sizeof(pcm));
    sent_capture_t c = {0};
    sensor_stream_session_send_data(s, pcm, sizeof(pcm), 0, capture_sent, &c);
    usleep(300 * 1000);
    CHECK(c.calls == 1 && c.last_ok, "voice send_data (uncompressed) completes ok");
    CHECK(srv.seen_wav_magic, "the wire body the mock server received starts with the RIFF magic - WAV-wrap happened before send");

    sensor_stream_session_destroy(s);
    pthread_join(th, NULL);
    close(srv.listen_fd);
}

/* Stream and Voice sessions are fully independent instances - confirmed
 * by running one of each concurrently against separate mock servers with
 * no shared state (each session owns its own tcp_channel worker/socket/
 * queue and its own result_policy). */
static void test_isolation(void)
{
    mock_server_t srv_s = {0}, srv_v = {0};
    CHECK(make_listener(&srv_s), "stream listener");
    CHECK(make_listener(&srv_v), "voice listener");
    srv_s.expected_connections = 1;
    srv_v.expected_connections = 1;
    pthread_t th_s, th_v;
    pthread_create(&th_s, NULL, mock_server_thread, &srv_s);
    pthread_create(&th_v, NULL, mock_server_thread, &srv_v);

    sensor_stream_config_t cfg_s = {0};
    cfg_s.server_ip = "127.0.0.1"; cfg_s.server_port = srv_s.port; cfg_s.device_serial = SERIAL; cfg_s.max_payload_size = 65536; cfg_s.danger_count_threshold = 4;
    sensor_stream_config_t cfg_v = cfg_s;
    cfg_v.server_port = srv_v.port;

    sensor_stream_session_t *stream_s = NULL, *voice_s = NULL;
    sensor_stream_session_create(&stream_s, SENSOR_STREAM_ROLE_STREAM, &cfg_s, NULL, NULL);
    sensor_stream_session_create(&voice_s, SENSOR_STREAM_ROLE_VOICE, &cfg_v, NULL, NULL);
    sensor_stream_session_start(stream_s);
    sensor_stream_session_start(voice_s);

    sent_capture_t cs = {0}, cv = {0};
    sensor_stream_session_send_pirin(stream_s, capture_sent, &cs);
    sensor_stream_session_send_pirin(voice_s, capture_sent, &cv);
    usleep(400 * 1000);
    CHECK(cs.calls == 1 && cs.last_ok, "stream session works independently");
    CHECK(cv.calls == 1 && cv.last_ok, "voice session works independently, concurrently, on a separate connection/port");

    sensor_stream_session_destroy(stream_s);
    sensor_stream_session_destroy(voice_s);
    pthread_join(th_s, NULL);
    pthread_join(th_v, NULL);
    close(srv_s.listen_fd);
    close(srv_v.listen_fd);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: %s <001|003|003wav|isolation>\n", argv[0]);
        return 2;
    }
    if (strcmp(argv[1], "001") == 0) {
        test_001();
    } else if (strcmp(argv[1], "003") == 0) {
        test_003();
    } else if (strcmp(argv[1], "003wav") == 0) {
        test_003_wav();
    } else if (strcmp(argv[1], "isolation") == 0) {
        test_isolation();
    } else {
        fprintf(stderr, "unknown test id: %s\n", argv[1]);
        return 2;
    }
    printf("\n=== SNS-STR-%s: %d passed, %d failed ===\n", argv[1], g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
