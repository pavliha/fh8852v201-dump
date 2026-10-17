#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#define RTSP_MAX_CLIENTS 8
#define RTSP_STREAM_COUNT 2
#define RTSP_TRACK_VIDEO 0
#define RTSP_TRACK_METADATA 1
#define RTSP_TRACK_COUNT 2

typedef struct {
    int active;
    int interleaved;
    uint8_t rtp_channel;
    uint8_t rtcp_channel;
    int rtp_fd;
    int rtcp_fd;
    struct sockaddr_in rtp_addr;
    struct sockaddr_in rtcp_addr;
    uint32_t ssrc;
    uint16_t seq;
} rtsp_track_t;

typedef struct {
    int tcp_fd;
    int loopback;
    int stream_id;
    int has_session;
    uint32_t session_id;
    int playing;
    int waiting_for_sps;
    uint64_t next_metadata_us;
    rtsp_track_t tracks[RTSP_TRACK_COUNT];
    char rxbuf[4096];
    size_t rxlen;
} rtsp_client_t;

typedef void (*rtsp_idr_request_fn)(void *ctx, int stream_id);

typedef struct {
    int listen_fd;
    rtsp_idr_request_fn request_idr;
    void *request_idr_ctx;
    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    int stream_enabled[RTSP_STREAM_COUNT];
    unsigned fps[RTSP_STREAM_COUNT];
    uint8_t sps[RTSP_STREAM_COUNT][128];
    size_t sps_len[RTSP_STREAM_COUNT];
    uint8_t pps[RTSP_STREAM_COUNT][128];
    size_t pps_len[RTSP_STREAM_COUNT];
    uint32_t video_ts_base[RTSP_STREAM_COUNT];
    uint32_t metadata_ts_base;
    int auth_enabled;
    char auth_user[32];
    char auth_pass[32];
} rtsp_server_t;

int rtsp_server_init(rtsp_server_t *srv, uint16_t port);
void rtsp_server_set_credentials(rtsp_server_t *srv, const char *user, const char *pass);
void rtsp_server_enable_stream(rtsp_server_t *srv, int stream_id, unsigned fps);
void rtsp_server_on_idr_request(rtsp_server_t *srv, rtsp_idr_request_fn fn, void *ctx);
void rtsp_server_poll(rtsp_server_t *srv);
void rtsp_server_push_nal(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len,
                          uint64_t pts_us, int is_last_in_frame);
void rtsp_server_close(rtsp_server_t *srv);

#endif
