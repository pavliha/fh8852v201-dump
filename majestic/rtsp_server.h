#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#define RTSP_MAX_CLIENTS 8
#define RTSP_STREAM_COUNT 2 /* stream1 = index 0 (main), stream2 = index 1 (sub) */

typedef struct {
    int tcp_fd;
    int rtp_fd;
    int rtcp_fd;
    struct sockaddr_in rtp_addr;
    uint32_t session_id;
    uint16_t seq;
    uint32_t ssrc;
    int playing;
    int stream_id;
    char rxbuf[2048];
    size_t rxlen;
} rtsp_client_t;

typedef struct {
    int listen_fd;
    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    uint8_t sps[RTSP_STREAM_COUNT][64];
    size_t sps_len[RTSP_STREAM_COUNT];
    uint8_t pps[RTSP_STREAM_COUNT][64];
    size_t pps_len[RTSP_STREAM_COUNT];
    uint32_t next_session_id;
    int auth_enabled;
    char auth_user[32];
    char auth_pass[32];
} rtsp_server_t;

int rtsp_server_init(rtsp_server_t *srv, uint16_t port);
void rtsp_server_set_credentials(rtsp_server_t *srv, const char *user, const char *pass);
void rtsp_server_poll(rtsp_server_t *srv);
void rtsp_server_set_param_set(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len);
void rtsp_server_send_nal(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len,
                           uint32_t rtp_ts, int is_last_in_frame);
void rtsp_server_close(rtsp_server_t *srv);

#endif
