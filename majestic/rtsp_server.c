#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "rtsp_server.h"

#define SERVER_NAME "OpenIPC.org RTSP Server/0.1"
#define AUTH_REALM "WallyWorld"
#define RTP_MAX_PAYLOAD 1200
#define FU_A_DATA (RTP_MAX_PAYLOAD - 2)
#define PT_H264 96
#define PT_METADATA 107
#define METADATA_INTERVAL_US 1000000u
#define NAL_SEI 6
#define NAL_SPS 7
#define NAL_PPS 8
#define NAL_FU_A 28

static const char b64_tab[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t random_u32(void)
{
    return ((uint32_t)random() << 16) ^ (uint32_t)random();
}

static void base64_encode(const uint8_t *in, size_t len, char *out)
{
    size_t o = 0;
    for (size_t i = 0; i < len; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < len) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < len) v |= in[i + 2];
        out[o++] = b64_tab[(v >> 18) & 0x3f];
        out[o++] = b64_tab[(v >> 12) & 0x3f];
        out[o++] = i + 1 < len ? b64_tab[(v >> 6) & 0x3f] : '=';
        out[o++] = i + 2 < len ? b64_tab[v & 0x3f] : '=';
    }
    out[o] = '\0';
}

static int b64_val(char c)
{
    const char *p = strchr(b64_tab, c);
    return c && p ? (int)(p - b64_tab) : -1;
}

static void base64_decode(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    uint32_t acc = 0;
    int bits = 0;
    for (; *in && *in != '='; in++) {
        int v = b64_val(*in);
        if (v < 0)
            continue;
        acc = (acc << 6) | (uint32_t)v;
        bits += 6;
        if (bits >= 8 && o + 1 < cap) {
            bits -= 8;
            out[o++] = (char)((acc >> bits) & 0xff);
        }
    }
    out[o] = '\0';
}

static int header_value(const char *req, const char *name, char *out, size_t cap)
{
    size_t n = strlen(name);
    for (const char *line = strstr(req, "\r\n"); line; line = strstr(line + 2, "\r\n")) {
        const char *p = line + 2;
        if (strncasecmp(p, name, n) != 0 || p[n] != ':')
            continue;
        p += n + 1;
        while (*p == ' ')
            p++;
        size_t i = 0;
        while (*p && *p != '\r' && i + 1 < cap)
            out[i++] = *p++;
        out[i] = '\0';
        return 1;
    }
    return 0;
}

static void send_all(int fd, const void *data, size_t len)
{
    const uint8_t *p = data;
    while (len > 0) {
        ssize_t n = send(fd, p, len, MSG_NOSIGNAL);
        if (n <= 0) {
            if (n < 0 && errno == EINTR)
                continue;
            return;
        }
        p += n;
        len -= (size_t)n;
    }
}

static void reply(rtsp_client_t *c, const char *cseq, const char *status, const char *extra)
{
    char buf[512];
    int n = snprintf(buf, sizeof(buf), "RTSP/1.0 %s\r\nCSeq: %s\r\nServer: " SERVER_NAME "\r\n%s\r\n",
                     status, cseq, extra ? extra : "");
    send_all(c->tcp_fd, buf, (size_t)n);
}

static void track_close(rtsp_track_t *t)
{
    if (t->rtp_fd >= 0)
        close(t->rtp_fd);
    if (t->rtcp_fd >= 0)
        close(t->rtcp_fd);
    memset(t, 0, sizeof(*t));
    t->rtp_fd = -1;
    t->rtcp_fd = -1;
}

static void session_reset(rtsp_client_t *c)
{
    for (int i = 0; i < RTSP_TRACK_COUNT; i++)
        track_close(&c->tracks[i]);
    c->has_session = 0;
    c->playing = 0;
    c->waiting_for_sps = 0;
}

static void client_init(rtsp_client_t *c)
{
    memset(c, 0, sizeof(*c));
    c->tcp_fd = -1;
    for (int i = 0; i < RTSP_TRACK_COUNT; i++) {
        c->tracks[i].rtp_fd = -1;
        c->tracks[i].rtcp_fd = -1;
    }
}

static void client_reset(rtsp_client_t *c)
{
    session_reset(c);
    if (c->tcp_fd >= 0)
        close(c->tcp_fd);
    client_init(c);
}

static void track_send(rtsp_client_t *c, rtsp_track_t *t, int rtcp, const uint8_t *data, size_t len)
{
    if (t->interleaved) {
        uint8_t frame[4 + 12 + RTP_MAX_PAYLOAD];
        if (len > sizeof(frame) - 4)
            return;
        frame[0] = '$';
        frame[1] = rtcp ? t->rtcp_channel : t->rtp_channel;
        frame[2] = (uint8_t)(len >> 8);
        frame[3] = (uint8_t)len;
        memcpy(frame + 4, data, len);
        send_all(c->tcp_fd, frame, len + 4);
        return;
    }
    if (rtcp)
        sendto(t->rtcp_fd, data, len, 0, (struct sockaddr *)&t->rtcp_addr, sizeof(t->rtcp_addr));
    else
        sendto(t->rtp_fd, data, len, 0, (struct sockaddr *)&t->rtp_addr, sizeof(t->rtp_addr));
}

static void rtp_send(rtsp_client_t *c, rtsp_track_t *t, uint8_t pt, int marker, uint32_t ts,
                     const uint8_t *prefix, size_t prefix_len, const uint8_t *payload, size_t len)
{
    uint8_t pkt[12 + RTP_MAX_PAYLOAD];
    pkt[0] = 0x80;
    pkt[1] = (uint8_t)((marker ? 0x80 : 0) | pt);
    pkt[2] = (uint8_t)(t->seq >> 8);
    pkt[3] = (uint8_t)t->seq;
    pkt[4] = (uint8_t)(ts >> 24);
    pkt[5] = (uint8_t)(ts >> 16);
    pkt[6] = (uint8_t)(ts >> 8);
    pkt[7] = (uint8_t)ts;
    pkt[8] = (uint8_t)(t->ssrc >> 24);
    pkt[9] = (uint8_t)(t->ssrc >> 16);
    pkt[10] = (uint8_t)(t->ssrc >> 8);
    pkt[11] = (uint8_t)t->ssrc;
    if (prefix_len)
        memcpy(pkt + 12, prefix, prefix_len);
    memcpy(pkt + 12 + prefix_len, payload, len);
    t->seq++;
    track_send(c, t, 0, pkt, 12 + prefix_len + len);
}

static void send_bye(rtsp_client_t *c, rtsp_track_t *t)
{
    static const char reason[] = "session ended";
    uint8_t pkt[24] = {0x81, 0xcb, 0x00, 0x05};
    pkt[4] = (uint8_t)(t->ssrc >> 24);
    pkt[5] = (uint8_t)(t->ssrc >> 16);
    pkt[6] = (uint8_t)(t->ssrc >> 8);
    pkt[7] = (uint8_t)t->ssrc;
    pkt[8] = (uint8_t)(sizeof(reason) - 1);
    memcpy(pkt + 9, reason, sizeof(reason) - 1);
    track_send(c, t, 1, pkt, sizeof(pkt));
}

static void send_metadata(rtsp_server_t *srv, rtsp_client_t *c)
{
    char utc[32];
    char xml[512];
    time_t now = time(NULL);
    struct tm tm;
    gmtime_r(&now, &tm);
    strftime(utc, sizeof(utc), "%Y-%m-%dT%H:%M:%SZ", &tm);
    int n = snprintf(xml, sizeof(xml),
                     "<tt:MetadataStream xmlns:tt=\"http://www.onvif.org/ver10/schema\"><tt:VideoAnalytics>"
                     "<tt:Frame UtcTime=\"%s\"><tt:Extension><tt:MotionInCells Columns=\"22\" Rows=\"18\" "
                     "Cells=\"zwA=\"/></tt:Extension></tt:Frame></tt:VideoAnalytics></tt:MetadataStream>",
                     utc);
    uint32_t ts = srv->metadata_ts_base + (uint32_t)(now_us() * 9u / 100u);
    rtp_send(c, &c->tracks[RTSP_TRACK_METADATA], PT_METADATA, 1, ts, NULL, 0, (const uint8_t *)xml, (size_t)n);
}

static int bind_udp_pair(rtsp_track_t *t)
{
    for (int attempt = 0; attempt < 16; attempt++) {
        int rtp = socket(AF_INET, SOCK_DGRAM, 0);
        int rtcp = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_ANY)};
        socklen_t al = sizeof(a);
        if (rtp >= 0 && rtcp >= 0 && bind(rtp, (struct sockaddr *)&a, sizeof(a)) == 0 &&
            getsockname(rtp, (struct sockaddr *)&a, &al) == 0) {
            a.sin_port = htons((uint16_t)(ntohs(a.sin_port) + 1));
            if (bind(rtcp, (struct sockaddr *)&a, sizeof(a)) == 0) {
                t->rtp_fd = rtp;
                t->rtcp_fd = rtcp;
                return ntohs(a.sin_port) - 1;
            }
        }
        if (rtp >= 0)
            close(rtp);
        if (rtcp >= 0)
            close(rtcp);
    }
    return -1;
}

static int stream_of(const char *uri)
{
    const char *p = strstr(uri, "stream=");
    return p ? atoi(p + strlen("stream=")) : 0;
}

static int ends_with(const char *s, const char *suffix)
{
    size_t n = strlen(s), m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static int auth_ok(rtsp_server_t *srv, rtsp_client_t *c, const char *req, const char *cseq)
{
    if (!srv->auth_enabled || c->loopback)
        return 1;
    char auth[256];
    const char *challenge = "WWW-Authenticate: Basic realm=\"" AUTH_REALM "\"\r\n";
    if (!header_value(req, "Authorization", auth, sizeof(auth))) {
        reply(c, cseq, "401 Authentication required", challenge);
        return 0;
    }
    char decoded[128] = {0};
    char expected[80];
    snprintf(expected, sizeof(expected), "%s:%s", srv->auth_user, srv->auth_pass);
    if (strncmp(auth, "Basic ", 6) == 0)
        base64_decode(auth + 6, decoded, sizeof(decoded));
    if (strcmp(decoded, expected) != 0) {
        reply(c, cseq, "401 Invalid credentials", challenge);
        return 0;
    }
    return 1;
}

static int session_ok(rtsp_client_t *c, const char *req, const char *cseq)
{
    char value[64];
    if (!header_value(req, "Session", value, sizeof(value))) {
        reply(c, cseq, "400 Failed to parse `Session'", NULL);
        return 0;
    }
    if (!c->has_session || strtoul(value, NULL, 10) != c->session_id) {
        reply(c, cseq, "454 Session ID is invalid", NULL);
        return 0;
    }
    return 1;
}

static int handle_describe(rtsp_server_t *srv, rtsp_client_t *c, const char *uri, const char *cseq)
{
    int s = stream_of(uri);
    if (s < 0 || s >= RTSP_STREAM_COUNT || !srv->stream_enabled[s])
        s = 0;
    if (!srv->sps_len[s] || !srv->pps_len[s])
        return 0;
    char sps64[192], pps64[192], sdp[1024], head[512];
    base64_encode(srv->sps[s], srv->sps_len[s], sps64);
    base64_encode(srv->pps[s], srv->pps_len[s], pps64);
    int sdp_len = snprintf(sdp, sizeof(sdp),
                           "v=0\r\no=OpenIPC 0 0 IN IP4 0.0.0.0\r\ns=Majestic\r\nc=IN IP4 0.0.0.0\r\nt=0 0\r\n"
                           "m=video 0 RTP/AVP 96\r\na=control:video\r\na=rtpmap:96 H264/90000\r\n"
                           "a=fmtp:96 profile-level-id=%02x%02x%02x;packetization-mode=1;sprop-parameter-sets=%s,%s\r\n"
                           "a=framerate:%u\r\n"
                           "m=application 0 RTP/AVP 107\r\na=rtpmap:107 vnd.onvif.metadata/90000\r\na=control:metadata\r\n",
                           srv->sps[s][1], srv->sps[s][2], srv->sps[s][3], sps64, pps64, srv->fps[s]);
    int head_len = snprintf(head, sizeof(head),
                            "RTSP/1.0 200 OK\r\nCSeq: %s\r\nContent-Length: %d\r\nServer: " SERVER_NAME
                            "\r\nContent-Base: %s\r\nContent-Type: application/sdp\r\n\r\n",
                            cseq, sdp_len, uri);
    send_all(c->tcp_fd, head, (size_t)head_len);
    send_all(c->tcp_fd, sdp, (size_t)sdp_len);
    return 1;
}

static void handle_setup(rtsp_server_t *srv, rtsp_client_t *c, const char *req, const char *uri, const char *cseq)
{
    int s = stream_of(uri);
    if (s < 0 || s >= RTSP_STREAM_COUNT || !srv->stream_enabled[s]) {
        reply(c, cseq, "404 Stream not found", NULL);
        return;
    }
    char transport[256] = {0};
    header_value(req, "Transport", transport, sizeof(transport));
    rtsp_track_t *t = &c->tracks[ends_with(uri, "metadata") ? RTSP_TRACK_METADATA : RTSP_TRACK_VIDEO];
    char reply_transport[160];
    unsigned a = 0, b = 0;
    if (strstr(transport, "TCP")) {
        const char *p = strstr(transport, "interleaved=");
        if (!p) {
            reply(c, cseq, "400 `interleaved' not found", NULL);
            return;
        }
        if (sscanf(p, "interleaved=%u-%u", &a, &b) < 2)
            b = a + 1;
        track_close(t);
        t->interleaved = 1;
        t->rtp_channel = (uint8_t)a;
        t->rtcp_channel = (uint8_t)b;
        snprintf(reply_transport, sizeof(reply_transport), "RTP/AVP/TCP;unicast;interleaved=%u-%u", a, b);
    } else {
        const char *p = strstr(transport, "client_port=");
        if (!p) {
            reply(c, cseq, "400 `client_port' not found", NULL);
            return;
        }
        if (sscanf(p, "client_port=%u-%u", &a, &b) < 2)
            b = a + 1;
        track_close(t);
        int port = bind_udp_pair(t);
        if (port < 0) {
            reply(c, cseq, "500 Internal Server Error", NULL);
            return;
        }
        struct sockaddr_in peer;
        socklen_t pl = sizeof(peer);
        getpeername(c->tcp_fd, (struct sockaddr *)&peer, &pl);
        t->rtp_addr = peer;
        t->rtp_addr.sin_port = htons((uint16_t)a);
        t->rtcp_addr = peer;
        t->rtcp_addr.sin_port = htons((uint16_t)b);
        snprintf(reply_transport, sizeof(reply_transport),
                 "RTP/AVP/UDP;unicast;client_port=%u-%u;server_port=%d-%d", a, b, port, port + 1);
    }
    t->active = 1;
    t->ssrc = random_u32();
    t->seq = 0;
    c->stream_id = s;
    if (!c->has_session) {
        c->has_session = 1;
        c->session_id = (uint32_t)random() & 0x7fffffffu;
    }
    char extra[256];
    snprintf(extra, sizeof(extra), "Transport: %s\r\nSession: %u;timeout=60\r\n", reply_transport, c->session_id);
    reply(c, cseq, "200 OK", extra);
}

static int handle_request(rtsp_server_t *srv, rtsp_client_t *c, const char *req)
{
    char method[32] = {0}, uri[256] = {0}, cseq[32] = {0};
    sscanf(req, "%31s %255s", method, uri);
    header_value(req, "CSeq", cseq, sizeof(cseq));
    if (!auth_ok(srv, c, req, cseq))
        return 1;
    if (strcmp(method, "OPTIONS") == 0) {
        reply(c, cseq, "200 OK", "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n");
    } else if (strcmp(method, "DESCRIBE") == 0) {
        return handle_describe(srv, c, uri, cseq);
    } else if (strcmp(method, "SETUP") == 0) {
        handle_setup(srv, c, req, uri, cseq);
    } else if (strcmp(method, "PLAY") == 0) {
        if (session_ok(c, req, cseq)) {
            reply(c, cseq, "200 OK", "Range: npt=now-\r\n");
            c->playing = 1;
            c->waiting_for_sps = 1;
            c->next_metadata_us = now_us();
        }
    } else if (strcmp(method, "GET_PARAMETER") == 0) {
        reply(c, cseq, "200 OK", NULL);
    } else if (strcmp(method, "TEARDOWN") == 0) {
        if (session_ok(c, req, cseq)) {
            for (int i = 0; i < RTSP_TRACK_COUNT; i++)
                if (c->tracks[i].active)
                    send_bye(c, &c->tracks[i]);
            reply(c, cseq, "200 OK", NULL);
            session_reset(c);
        }
    } else {
        reply(c, cseq, "405 Unknown method", NULL);
    }
    return 1;
}

static void drain_requests(rtsp_server_t *srv, rtsp_client_t *c)
{
    for (;;) {
        char *end = strstr(c->rxbuf, "\r\n\r\n");
        if (!end)
            return;
        size_t req_len = (size_t)(end - c->rxbuf) + 4;
        char req[sizeof(c->rxbuf)];
        memcpy(req, c->rxbuf, req_len);
        req[req_len] = '\0';
        if (!handle_request(srv, c, req))
            return;
        memmove(c->rxbuf, c->rxbuf + req_len, c->rxlen - req_len + 1);
        c->rxlen -= req_len;
    }
}

int rtsp_server_init(rtsp_server_t *srv, uint16_t port)
{
    memset(srv, 0, sizeof(*srv));
    srandom((unsigned)(time(NULL) ^ (getpid() << 16) ^ now_us()));
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++)
        client_init(&srv->clients[i]);
    for (int s = 0; s < RTSP_STREAM_COUNT; s++)
        srv->video_ts_base[s] = random_u32();
    srv->metadata_ts_base = random_u32();
    srv->listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (srv->listen_fd < 0)
        return -1;
    int one = 1;
    setsockopt(srv->listen_fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port), .sin_addr.s_addr = htonl(INADDR_ANY)};
    if (bind(srv->listen_fd, (struct sockaddr *)&a, sizeof(a)) < 0 || listen(srv->listen_fd, 8) < 0) {
        close(srv->listen_fd);
        return -1;
    }
    fcntl(srv->listen_fd, F_SETFL, O_NONBLOCK);
    return 0;
}

void rtsp_server_set_credentials(rtsp_server_t *srv, const char *user, const char *pass)
{
    snprintf(srv->auth_user, sizeof(srv->auth_user), "%s", user);
    snprintf(srv->auth_pass, sizeof(srv->auth_pass), "%s", pass);
    srv->auth_enabled = 1;
}

void rtsp_server_enable_stream(rtsp_server_t *srv, int stream_id, unsigned fps)
{
    if (stream_id < 0 || stream_id >= RTSP_STREAM_COUNT)
        return;
    srv->stream_enabled[stream_id] = 1;
    srv->fps[stream_id] = fps;
}

void rtsp_server_poll(rtsp_server_t *srv)
{
    struct sockaddr_in peer;
    socklen_t pl = sizeof(peer);
    int fd = accept(srv->listen_fd, (struct sockaddr *)&peer, &pl);
    if (fd >= 0) {
        rtsp_client_t *slot = NULL;
        for (int i = 0; i < RTSP_MAX_CLIENTS && !slot; i++)
            if (srv->clients[i].tcp_fd < 0)
                slot = &srv->clients[i];
        if (!slot) {
            close(fd);
        } else {
            client_reset(slot);
            slot->tcp_fd = fd;
            slot->loopback = (ntohl(peer.sin_addr.s_addr) >> 24) == 127;
            fcntl(fd, F_SETFL, O_NONBLOCK);
        }
    }
    uint64_t now = now_us();
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        rtsp_client_t *c = &srv->clients[i];
        if (c->tcp_fd < 0)
            continue;
        ssize_t n = recv(c->tcp_fd, c->rxbuf + c->rxlen, sizeof(c->rxbuf) - 1 - c->rxlen, 0);
        if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)) {
            client_reset(c);
            continue;
        }
        if (n > 0)
            c->rxlen += (size_t)n;
        c->rxbuf[c->rxlen] = '\0';
        drain_requests(srv, c);
        if (c->rxlen >= sizeof(c->rxbuf) - 1)
            c->rxlen = 0;
        if (c->playing && c->tracks[RTSP_TRACK_METADATA].active && now >= c->next_metadata_us) {
            send_metadata(srv, c);
            c->next_metadata_us += METADATA_INTERVAL_US;
        }
    }
}

static void cache_param_set(rtsp_server_t *srv, int s, const uint8_t *nal, size_t len)
{
    int type = nal[0] & 0x1f;
    if ((type != NAL_SPS && type != NAL_PPS) || len > sizeof(srv->sps[0]))
        return;
    memcpy(type == NAL_SPS ? srv->sps[s] : srv->pps[s], nal, len);
    *(type == NAL_SPS ? &srv->sps_len[s] : &srv->pps_len[s]) = len;
}

static void send_video_nal(rtsp_client_t *c, const uint8_t *nal, size_t len, uint32_t ts, int last)
{
    rtsp_track_t *t = &c->tracks[RTSP_TRACK_VIDEO];
    if (len <= RTP_MAX_PAYLOAD) {
        rtp_send(c, t, PT_H264, last, ts, NULL, 0, nal, len);
        return;
    }
    uint8_t fu[2] = {(uint8_t)((nal[0] & 0xe0) | NAL_FU_A), (uint8_t)(0x80 | (nal[0] & 0x1f))};
    for (size_t off = 1; off < len; off += FU_A_DATA) {
        size_t chunk = len - off < FU_A_DATA ? len - off : FU_A_DATA;
        int end = off + chunk == len;
        if (end)
            fu[1] |= 0x40;
        rtp_send(c, t, PT_H264, last && end, ts, fu, 2, nal + off, chunk);
        fu[1] &= 0x7f;
    }
}

void rtsp_server_push_nal(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len,
                          uint64_t pts_us, int is_last_in_frame)
{
    if (stream_id < 0 || stream_id >= RTSP_STREAM_COUNT || nal_len == 0)
        return;
    cache_param_set(srv, stream_id, nal, nal_len);
    int type = nal[0] & 0x1f;
    if (type == NAL_SEI)
        return;
    uint32_t ts = srv->video_ts_base[stream_id] + (uint32_t)(pts_us * 9u / 100u);
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        rtsp_client_t *c = &srv->clients[i];
        if (c->tcp_fd < 0 || !c->playing || c->stream_id != stream_id || !c->tracks[RTSP_TRACK_VIDEO].active)
            continue;
        if (c->waiting_for_sps) {
            if (type != NAL_SPS)
                continue;
            c->waiting_for_sps = 0;
        }
        send_video_nal(c, nal, nal_len, ts, is_last_in_frame);
    }
}

void rtsp_server_close(rtsp_server_t *srv)
{
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++)
        client_reset(&srv->clients[i]);
    if (srv->listen_fd >= 0)
        close(srv->listen_fd);
}
