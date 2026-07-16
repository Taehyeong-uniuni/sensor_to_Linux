#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "savvy/protocol/packet_codec.h"
#include "sensor_platform/tcp_channel.h"

static const uint8_t SERIAL[SAVVY_PACKET_SERIAL_LEN] = {
    '0','0','0','0','0','0','0','0','0','0','0','0','0','1'
};

typedef struct child_server {
    pid_t pid;
    uint16_t port;
} child_server_t;

typedef struct completion_capture {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    int calls;
    sensor_tcp_result_status_t status;
    uint8_t command;
    bool crc_valid;
} completion_capture_t;

static int64_t monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + (int64_t)(ts.tv_nsec / 1000000);
}

static void sleep_ms(unsigned int ms)
{
    struct timespec req;
    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)(ms % 1000u) * 1000000L;
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
}

static bool read_child_port(int fd, uint16_t *out_port)
{
    char line[64];
    size_t used = 0;
    int64_t deadline = monotonic_ms() + 2000;

    while (used + 1u < sizeof(line)) {
        int64_t remaining = deadline - monotonic_ms();
        if (remaining <= 0) {
            return false;
        }
        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;
        int ready = poll(&pfd, 1, (int)remaining);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            return false;
        }

        char c;
        ssize_t n = read(fd, &c, 1);
        if (n != 1) {
            return false;
        }
        line[used++] = c;
        if (c == '\n') {
            break;
        }
    }
    line[used] = '\0';

    unsigned int parsed = 0;
    if (sscanf(line, "PORT %u", &parsed) != 1 || parsed == 0 || parsed > UINT16_MAX) {
        return false;
    }
    *out_port = (uint16_t)parsed;
    return true;
}

static void force_reap(child_server_t *child)
{
    if (child->pid <= 0) {
        return;
    }
    (void)kill(child->pid, SIGKILL);
    (void)waitpid(child->pid, NULL, 0);
    child->pid = -1;
}

static bool start_server(const char *server_path, const char *fixture, child_server_t *out_child)
{
    int output_pipe[2];
    if (pipe(output_pipe) != 0) {
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        close(output_pipe[0]);
        close(output_pipe[1]);
        return false;
    }
    if (pid == 0) {
        char fixture_arg[128];
        if (snprintf(fixture_arg, sizeof(fixture_arg), "--fixture=%s", fixture) < 0) {
            _exit(126);
        }
        close(output_pipe[0]);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0) {
            _exit(126);
        }
        close(output_pipe[1]);
        execl(server_path, server_path, fixture_arg, "--port=0", (char *)NULL);
        _exit(127);
    }

    close(output_pipe[1]);
    out_child->pid = pid;
    out_child->port = 0;
    bool got_port = read_child_port(output_pipe[0], &out_child->port);
    close(output_pipe[0]);
    if (!got_port) {
        force_reap(out_child);
        return false;
    }
    return true;
}

static bool wait_child_normal(child_server_t *child, unsigned int timeout_ms)
{
    int64_t deadline = monotonic_ms() + (int64_t)timeout_ms;
    while (monotonic_ms() < deadline) {
        int status = 0;
        pid_t got = waitpid(child->pid, &status, WNOHANG);
        if (got == child->pid) {
            child->pid = -1;
            return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        }
        if (got < 0) {
            child->pid = -1;
            return false;
        }
        sleep_ms(10);
    }
    force_reap(child);
    return false;
}

static void capture_init(completion_capture_t *capture)
{
    memset(capture, 0, sizeof(*capture));
    pthread_mutex_init(&capture->mutex, NULL);
    pthread_cond_init(&capture->cond, NULL);
}

static void capture_destroy(completion_capture_t *capture)
{
    pthread_cond_destroy(&capture->cond);
    pthread_mutex_destroy(&capture->mutex);
}

static void capture_complete(const sensor_tcp_result_t *result, void *ctx)
{
    completion_capture_t *capture = (completion_capture_t *)ctx;
    pthread_mutex_lock(&capture->mutex);
    capture->calls++;
    capture->status = result->status;
    capture->command = result->command;
    capture->crc_valid = result->crc_valid;
    pthread_cond_signal(&capture->cond);
    pthread_mutex_unlock(&capture->mutex);
}

static bool wait_completion(completion_capture_t *capture, unsigned int timeout_ms)
{
    struct timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += (time_t)(timeout_ms / 1000u);
    deadline.tv_nsec += (long)(timeout_ms % 1000u) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }

    pthread_mutex_lock(&capture->mutex);
    while (capture->calls == 0) {
        int rc = pthread_cond_timedwait(&capture->cond, &capture->mutex, &deadline);
        if (rc == ETIMEDOUT) {
            pthread_mutex_unlock(&capture->mutex);
            return false;
        }
        if (rc != 0) {
            pthread_mutex_unlock(&capture->mutex);
            return false;
        }
    }
    pthread_mutex_unlock(&capture->mutex);
    return true;
}

static bool encode_request(uint8_t start, uint8_t command, uint8_t *packet, size_t *packet_len)
{
    static const uint8_t DATA[] = {0x42, 0x24, 0x11, 0x7f};
    return savvy_packet_encode(start, command, 0, 0, SERIAL, sizeof(SERIAL),
                               DATA, sizeof(DATA), packet, 128, packet_len) == SAVVY_OK;
}

static bool run_one_fixture(const char *server_path, const char *fixture,
                            uint32_t response_timeout_ms,
                            sensor_tcp_result_status_t expected_status)
{
    child_server_t child = {.pid = -1, .port = 0};
    if (!start_server(server_path, fixture, &child)) {
        fprintf(stderr, "FAIL: %s child server did not publish an ephemeral port\n", fixture);
        return false;
    }

    sensor_tcp_channel_t *channel = NULL;
    if (sensor_tcp_channel_create(&channel, "127.0.0.1", child.port, 4096, 2) != SAVVY_OK ||
        sensor_tcp_channel_start(channel) != SAVVY_OK) {
        fprintf(stderr, "FAIL: %s production TCP channel setup failed\n", fixture);
        sensor_tcp_channel_destroy(channel);
        force_reap(&child);
        return false;
    }

    uint8_t packet[128];
    size_t packet_len = 0;
    completion_capture_t capture;
    capture_init(&capture);
    bool submitted = encode_request((uint8_t)'S', (uint8_t)'S', packet, &packet_len) &&
                     sensor_tcp_channel_submit(channel, packet, packet_len, response_timeout_ms,
                                               capture_complete, &capture) == SAVVY_OK;
    bool completed = submitted && wait_completion(&capture, 2000);

    pthread_mutex_lock(&capture.mutex);
    bool outcome_ok = completed && capture.calls == 1 && capture.status == expected_status;
    if (expected_status == SENSOR_TCP_OK) {
        outcome_ok = outcome_ok && capture.command == (uint8_t)'R' && capture.crc_valid;
    }
    pthread_mutex_unlock(&capture.mutex);

    sensor_tcp_channel_destroy(channel);
    bool child_ok = wait_child_normal(&child, 2000);
    capture_destroy(&capture);

    if (!submitted || !completed || !outcome_ok || !child_ok) {
        fprintf(stderr,
                "FAIL: fixture=%s submitted=%d completed=%d outcome=%d child_normal=%d\n",
                fixture, submitted ? 1 : 0, completed ? 1 : 0,
                outcome_ok ? 1 : 0, child_ok ? 1 : 0);
        return false;
    }
    return true;
}

static bool run_channel_isolation(const char *server_path)
{
    child_server_t stream_child = {.pid = -1, .port = 0};
    child_server_t voice_child = {.pid = -1, .port = 0};
    if (!start_server(server_path, "stream-only-failure", &stream_child) ||
        !start_server(server_path, "v-r-normal", &voice_child)) {
        fprintf(stderr, "FAIL: isolation child startup\n");
        force_reap(&stream_child);
        force_reap(&voice_child);
        return false;
    }

    sensor_tcp_channel_t *stream = NULL;
    sensor_tcp_channel_t *voice = NULL;
    bool setup_ok = sensor_tcp_channel_create(&stream, "127.0.0.1", stream_child.port, 4096, 2) == SAVVY_OK &&
                    sensor_tcp_channel_create(&voice, "127.0.0.1", voice_child.port, 4096, 2) == SAVVY_OK &&
                    sensor_tcp_channel_start(stream) == SAVVY_OK &&
                    sensor_tcp_channel_start(voice) == SAVVY_OK;
    if (!setup_ok) {
        fprintf(stderr, "FAIL: isolation production TCP setup\n");
        sensor_tcp_channel_destroy(stream);
        sensor_tcp_channel_destroy(voice);
        force_reap(&stream_child);
        force_reap(&voice_child);
        return false;
    }

    uint8_t stream_packet[128], voice_packet[128];
    size_t stream_len = 0, voice_len = 0;
    completion_capture_t stream_capture, voice_capture;
    capture_init(&stream_capture);
    capture_init(&voice_capture);
    bool submitted = encode_request((uint8_t)'S', (uint8_t)'S', stream_packet, &stream_len) &&
                     encode_request((uint8_t)'V', (uint8_t)'V', voice_packet, &voice_len) &&
                     sensor_tcp_channel_submit(stream, stream_packet, stream_len, 1000,
                                               capture_complete, &stream_capture) == SAVVY_OK &&
                     sensor_tcp_channel_submit(voice, voice_packet, voice_len, 1000,
                                               capture_complete, &voice_capture) == SAVVY_OK;
    bool completed = submitted && wait_completion(&stream_capture, 2000) && wait_completion(&voice_capture, 2000);

    pthread_mutex_lock(&stream_capture.mutex);
    bool stream_failed = stream_capture.calls == 1 && stream_capture.status != SENSOR_TCP_OK;
    pthread_mutex_unlock(&stream_capture.mutex);
    pthread_mutex_lock(&voice_capture.mutex);
    bool voice_healthy = voice_capture.calls == 1 && voice_capture.status == SENSOR_TCP_OK &&
                         voice_capture.command == (uint8_t)'R' && voice_capture.crc_valid;
    pthread_mutex_unlock(&voice_capture.mutex);

    sensor_tcp_channel_destroy(stream);
    sensor_tcp_channel_destroy(voice);
    /* Two sanitizer-instrumented child processes may spend noticeably
     * longer in runtime shutdown under arm64 emulation. Keep the wait
     * bounded, but allow enough time to observe their normal exit. */
    bool stream_child_ok = wait_child_normal(&stream_child, 5000);
    bool voice_child_ok = wait_child_normal(&voice_child, 5000);
    capture_destroy(&stream_capture);
    capture_destroy(&voice_capture);

    if (!completed || !stream_failed || !voice_healthy || !stream_child_ok || !voice_child_ok) {
        fprintf(stderr,
                "FAIL: isolation completed=%d stream_failed=%d voice_healthy=%d children=%d/%d\n",
                completed ? 1 : 0, stream_failed ? 1 : 0, voice_healthy ? 1 : 0,
                stream_child_ok ? 1 : 0, voice_child_ok ? 1 : 0);
        return false;
    }
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <mock-server-path> <header|body|delay|timeout|late|disconnect-header|disconnect-body|isolation>\n", argv[0]);
        return 2;
    }

    const char *server_path = argv[1];
    const char *mode = argv[2];
    bool ok = false;
    if (strcmp(mode, "header") == 0) {
        ok = run_one_fixture(server_path, "partial-header", 1000, SENSOR_TCP_OK);
    } else if (strcmp(mode, "body") == 0) {
        ok = run_one_fixture(server_path, "split-body", 1000, SENSOR_TCP_OK);
    } else if (strcmp(mode, "delay") == 0) {
        ok = run_one_fixture(server_path, "delay-then-respond", 1000, SENSOR_TCP_OK);
    } else if (strcmp(mode, "timeout") == 0) {
        ok = run_one_fixture(server_path, "delay-exceeds-timeout", 250, SENSOR_TCP_ERR_RESPONSE_TIMEOUT);
    } else if (strcmp(mode, "late") == 0) {
        ok = run_one_fixture(server_path, "stale-response-after-timeout", 250, SENSOR_TCP_ERR_RESPONSE_TIMEOUT);
    } else if (strcmp(mode, "disconnect-header") == 0) {
        ok = run_one_fixture(server_path, "disconnect-before-header", 1000, SENSOR_TCP_ERR_DISCONNECTED);
    } else if (strcmp(mode, "disconnect-body") == 0) {
        ok = run_one_fixture(server_path, "disconnect-during-body", 1000, SENSOR_TCP_ERR_DISCONNECTED);
    } else if (strcmp(mode, "isolation") == 0) {
        ok = run_channel_isolation(server_path);
    } else {
        fprintf(stderr, "unknown mode: %s\n", mode);
        return 2;
    }

    printf("mock response fixture %s: %s\n", mode, ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
