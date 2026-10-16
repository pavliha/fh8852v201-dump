#ifndef __RTSP_SERVER_H__
#define __RTSP_SERVER_H__

#include <stdint.h>
#include <stddef.h>
#include <netinet/in.h>

#define RTSP_MAX_CLIENTS 8

typedef struct {
    int tcp_fd;
    int rtp_fd;
    int rtcp_fd;
    struct sockaddr_in rtp_addr;
    uint32_t session_id;
    uint16_t seq;
    uint32_t ssrc;
    int playing;
    char rxbuf[2048];
    size_t rxlen;
} rtsp_client_t;

typedef struct {
    int listen_fd;
    rtsp_client_t clients[RTSP_MAX_CLIENTS];
    uint8_t sps[64];
    size_t sps_len;
    uint8_t pps[64];
    size_t pps_len;
    uint32_t next_session_id;
} rtsp_server_t;

int rtsp_server_init(rtsp_server_t *srv, uint16_t port);
void rtsp_server_poll(rtsp_server_t *srv);
void rtsp_server_set_param_set(rtsp_server_t *srv, const uint8_t *nal, size_t nal_len);
void rtsp_server_send_nal(rtsp_server_t *srv, const uint8_t *nal, size_t nal_len,
                           uint32_t rtp_ts, int is_last_in_frame);
void rtsp_server_close(rtsp_server_t *srv);

#endif
