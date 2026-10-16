/**
 * Minimal RTSP/RTP server for mini_streamer.c.
 *
 * Implemented directly from RFC 2326 (RTSP) and RFC 6184 (H.264 over RTP) —
 * public IETF specs, not decompiled from any vendor binary. There is no
 * vendor code to clean-room against here: majestic's own RTSP server, and
 * ipcam's rtsp-prefixed/m_rtsp-prefixed/zrtsp-prefixed functions, were
 * deliberately not consulted for protocol logic (see README's decompiled-
 * function tables for their existence, not their implementation).
 *
 * Scope: two video streams (stream1 = main, stream2 = sub), H.264 only,
 * UDP transport, up to RTSP_MAX_CLIENTS simultaneous viewers, optional
 * RTSP Basic auth. No RTCP feedback, no TCP-interleaved mode, no audio
 * track. OPTIONS/DESCRIBE/SETUP/PLAY/TEARDOWN/GET_PARAMETER only.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "rtsp_server.h"

static const char b64_tab[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static size_t base64_encode(const uint8_t *in, size_t in_len, char *out, size_t out_cap)
{
    size_t oi = 0, i = 0;
    for (; i + 3 <= in_len && oi + 4 < out_cap; i += 3) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8) | in[i + 2];
        out[oi++] = b64_tab[(v >> 18) & 0x3F];
        out[oi++] = b64_tab[(v >> 12) & 0x3F];
        out[oi++] = b64_tab[(v >> 6) & 0x3F];
        out[oi++] = b64_tab[v & 0x3F];
    }
    size_t rem = in_len - i;
    if (rem == 1 && oi + 4 < out_cap) {
        uint32_t v = (uint32_t)in[i] << 16;
        out[oi++] = b64_tab[(v >> 18) & 0x3F];
        out[oi++] = b64_tab[(v >> 12) & 0x3F];
        out[oi++] = '=';
        out[oi++] = '=';
    } else if (rem == 2 && oi + 4 < out_cap) {
        uint32_t v = ((uint32_t)in[i] << 16) | ((uint32_t)in[i + 1] << 8);
        out[oi++] = b64_tab[(v >> 18) & 0x3F];
        out[oi++] = b64_tab[(v >> 12) & 0x3F];
        out[oi++] = b64_tab[(v >> 6) & 0x3F];
        out[oi++] = '=';
    }
    out[oi] = '\0';
    return oi;
}

static int b64_val(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

static size_t base64_decode(const char *in, char *out, size_t out_cap)
{
    size_t oi = 0;
    int vals[4];
    int n = 0;
    for (const char *p = in; *p && *p != '='; p++) {
        int v = b64_val(*p);
        if (v < 0) continue;
        vals[n++] = v;
        if (n == 4) {
            if (oi + 3 < out_cap) {
                out[oi++] = (char)((vals[0] << 2) | (vals[1] >> 4));
                out[oi++] = (char)((vals[1] << 4) | (vals[2] >> 2));
                out[oi++] = (char)((vals[2] << 6) | vals[3]);
            }
            n = 0;
        }
    }
    if (n >= 2 && oi + 1 < out_cap) out[oi++] = (char)((vals[0] << 2) | (vals[1] >> 4));
    if (n >= 3 && oi + 1 < out_cap) out[oi++] = (char)((vals[1] << 4) | (vals[2] >> 2));
    out[oi] = '\0';
    return oi;
}

/* Exact-case match on the canonical RTSP header spelling ffplay/VLC send.
   RTSP headers are formally case-insensitive; this is a scoped-down
   simplification, not a protocol-complete parser. */
static int header_value(const char *req, const char *name, char *out, size_t out_cap)
{
    size_t name_len = strlen(name);
    const char *p = req;
    while ((p = strstr(p, name)) != NULL) {
        if (p == req || p[-1] == '\n') {
            const char *v = p + name_len;
            while (*v == ' ' || *v == ':') v++;
            size_t i = 0;
            while (*v && *v != '\r' && *v != '\n' && i + 1 < out_cap) out[i++] = *v++;
            out[i] = '\0';
            return 1;
        }
        p++;
    }
    return 0;
}

/* Which stream a request's URI refers to: anything mentioning "stream2"
   is the sub stream (index 1), everything else defaults to stream1. */
static int parse_stream_id(const char *req)
{
    char uri[256] = {0};
    sscanf(req, "%*s %255s", uri);
    return strstr(uri, "stream2") != NULL ? 1 : 0;
}

static int check_auth(rtsp_server_t *srv, const char *req)
{
    if (!srv->auth_enabled) return 1;
    char auth[256] = {0};
    if (!header_value(req, "Authorization", auth, sizeof(auth))) return 0;
    char *b64 = strstr(auth, "Basic ");
    if (!b64) return 0;
    b64 += strlen("Basic ");
    char decoded[128] = {0};
    base64_decode(b64, decoded, sizeof(decoded));
    char expected[64];
    snprintf(expected, sizeof(expected), "%s:%s", srv->auth_user, srv->auth_pass);
    return strcmp(decoded, expected) == 0;
}

static void rtsp_client_reset(rtsp_client_t *c)
{
    if (c->tcp_fd >= 0) close(c->tcp_fd);
    if (c->rtp_fd >= 0) close(c->rtp_fd);
    if (c->rtcp_fd >= 0) close(c->rtcp_fd);
    memset(c, 0, sizeof(*c));
    c->tcp_fd = -1;
    c->rtp_fd = -1;
    c->rtcp_fd = -1;
}

int rtsp_server_init(rtsp_server_t *srv, uint16_t port)
{
    memset(srv, 0, sizeof(*srv));
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        srv->clients[i].tcp_fd = -1;
        srv->clients[i].rtp_fd = -1;
        srv->clients[i].rtcp_fd = -1;
    }
    srv->next_session_id = 1;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }
    if (listen(fd, 8) < 0) {
        close(fd);
        return -1;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    srv->listen_fd = fd;
    return 0;
}

void rtsp_server_set_credentials(rtsp_server_t *srv, const char *user, const char *pass)
{
    srv->auth_enabled = 1;
    strncpy(srv->auth_user, user, sizeof(srv->auth_user) - 1);
    strncpy(srv->auth_pass, pass, sizeof(srv->auth_pass) - 1);
}

static void rtsp_accept_new(rtsp_server_t *srv)
{
    int fd = accept(srv->listen_fd, NULL, NULL);
    if (fd < 0) return;

    int slot = -1;
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (srv->clients[i].tcp_fd < 0) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        close(fd);
        return;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    rtsp_client_reset(&srv->clients[slot]);
    srv->clients[slot].tcp_fd = fd;
}

static void rtsp_handle_request(rtsp_server_t *srv, rtsp_client_t *c, const char *req)
{
    char method[16] = {0};
    sscanf(req, "%15s", method);

    char cseq[32] = "0";
    header_value(req, "CSeq", cseq, sizeof(cseq));

    char resp[1024];
    int resp_len;

    int needs_auth = (strcmp(method, "DESCRIBE") == 0 || strcmp(method, "SETUP") == 0 ||
                       strcmp(method, "PLAY") == 0);
    if (needs_auth && !check_auth(srv, req)) {
        resp_len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 401 Unauthorized\r\nCSeq: %s\r\n"
            "WWW-Authenticate: Basic realm=\"mini_streamer\"\r\n\r\n", cseq);
        send(c->tcp_fd, resp, (size_t)resp_len, 0);
        return;
    }

    if (strcmp(method, "OPTIONS") == 0) {
        resp_len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
            "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN, GET_PARAMETER\r\n\r\n",
            cseq);
    } else if (strcmp(method, "DESCRIBE") == 0) {
        int sid = parse_stream_id(req);
        char sps_b64[128] = {0}, pps_b64[128] = {0};
        base64_encode(srv->sps[sid], srv->sps_len[sid], sps_b64, sizeof(sps_b64));
        base64_encode(srv->pps[sid], srv->pps_len[sid], pps_b64, sizeof(pps_b64));
        char sdp[768];
        int sdp_len = snprintf(sdp, sizeof(sdp),
            "v=0\r\n"
            "o=- 0 0 IN IP4 0.0.0.0\r\n"
            "s=mini_streamer\r\n"
            "c=IN IP4 0.0.0.0\r\n"
            "t=0 0\r\n"
            "a=tool:mini_streamer\r\n"
            "m=video 0 RTP/AVP 96\r\n"
            "a=rtpmap:96 H264/90000\r\n"
            "a=fmtp:96 packetization-mode=1;profile-level-id=%02x%02x%02x;"
            "sprop-parameter-sets=%s,%s\r\n"
            "a=control:stream%d\r\n",
            srv->sps_len[sid] > 3 ? srv->sps[sid][1] : 0,
            srv->sps_len[sid] > 3 ? srv->sps[sid][2] : 0,
            srv->sps_len[sid] > 3 ? srv->sps[sid][3] : 0,
            sps_b64, pps_b64, sid + 1);
        resp_len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\n"
            "Content-Base: rtsp://0.0.0.0:554/stream%d/\r\n"
            "Content-Type: application/sdp\r\nContent-Length: %d\r\n\r\n%s",
            cseq, sid + 1, sdp_len, sdp);
    } else if (strcmp(method, "SETUP") == 0) {
        char transport[256] = {0};
        header_value(req, "Transport", transport, sizeof(transport));
        int cport1 = 0, cport2 = 0;
        char *cp = strstr(transport, "client_port=");
        if (cp) sscanf(cp + strlen("client_port="), "%d-%d", &cport1, &cport2);

        if (cport1 <= 0) {
            resp_len = snprintf(resp, sizeof(resp),
                "RTSP/1.0 461 Unsupported Transport\r\nCSeq: %s\r\n\r\n", cseq);
        } else {
            struct sockaddr_in peer;
            socklen_t plen = sizeof(peer);
            getpeername(c->tcp_fd, (struct sockaddr *)&peer, &plen);
            c->rtp_addr = peer;
            c->rtp_addr.sin_port = htons((uint16_t)cport1);

            c->rtp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            c->rtcp_fd = socket(AF_INET, SOCK_DGRAM, 0);
            struct sockaddr_in local;
            memset(&local, 0, sizeof(local));
            local.sin_family = AF_INET;
            local.sin_addr.s_addr = INADDR_ANY;

            if (c->rtp_fd < 0 || c->rtcp_fd < 0 ||
                bind(c->rtp_fd, (struct sockaddr *)&local, sizeof(local)) < 0 ||
                bind(c->rtcp_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
                if (c->rtp_fd >= 0) { close(c->rtp_fd); c->rtp_fd = -1; }
                if (c->rtcp_fd >= 0) { close(c->rtcp_fd); c->rtcp_fd = -1; }
                resp_len = snprintf(resp, sizeof(resp),
                    "RTSP/1.0 500 Internal Server Error\r\nCSeq: %s\r\n\r\n", cseq);
                send(c->tcp_fd, resp, (size_t)resp_len, 0);
                return;
            }

            socklen_t llen = sizeof(local);
            getsockname(c->rtp_fd, (struct sockaddr *)&local, &llen);
            uint16_t server_rtp_port = ntohs(local.sin_port);
            getsockname(c->rtcp_fd, (struct sockaddr *)&local, &llen);
            uint16_t server_rtcp_port = ntohs(local.sin_port);

            c->session_id = srv->next_session_id++;
            c->seq = 0;
            c->ssrc = 0x4D494E49u + c->session_id; /* "MINI" + session id */
            c->playing = 0;
            c->stream_id = parse_stream_id(req);

            resp_len = snprintf(resp, sizeof(resp),
                "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %u\r\n"
                "Transport: RTP/AVP;unicast;client_port=%d-%d;server_port=%u-%u;ssrc=%08X\r\n\r\n",
                cseq, c->session_id, cport1, cport2, server_rtp_port, server_rtcp_port, c->ssrc);
        }
    } else if (strcmp(method, "PLAY") == 0) {
        c->playing = (c->rtp_fd >= 0);
        resp_len = snprintf(resp, sizeof(resp),
            "RTSP/1.0 200 OK\r\nCSeq: %s\r\nSession: %u\r\nRange: npt=0.000-\r\n\r\n",
            cseq, c->session_id);
    } else if (strcmp(method, "TEARDOWN") == 0) {
        resp_len = snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
        send(c->tcp_fd, resp, (size_t)resp_len, 0);
        rtsp_client_reset(c);
        return;
    } else if (strcmp(method, "GET_PARAMETER") == 0) {
        resp_len = snprintf(resp, sizeof(resp), "RTSP/1.0 200 OK\r\nCSeq: %s\r\n\r\n", cseq);
    } else {
        resp_len = snprintf(resp, sizeof(resp), "RTSP/1.0 501 Not Implemented\r\nCSeq: %s\r\n\r\n", cseq);
    }
    send(c->tcp_fd, resp, (size_t)resp_len, 0);
}

static void rtsp_service_client(rtsp_server_t *srv, int idx)
{
    rtsp_client_t *c = &srv->clients[idx];
    char tmp[1024];
    ssize_t n = recv(c->tcp_fd, tmp, sizeof(tmp) - 1, MSG_DONTWAIT);
    if (n == 0) {
        rtsp_client_reset(c);
        return;
    }
    if (n < 0) return; /* EAGAIN or transient error; TEARDOWN/reset handles real disconnects */

    if (c->rxlen + (size_t)n >= sizeof(c->rxbuf)) {
        c->rxlen = 0; /* overflow guard: drop and resync on the next request */
        return;
    }
    memcpy(c->rxbuf + c->rxlen, tmp, (size_t)n);
    c->rxlen += (size_t)n;
    c->rxbuf[c->rxlen] = '\0';

    if (strstr(c->rxbuf, "\r\n\r\n") == NULL) return;
    rtsp_handle_request(srv, c, c->rxbuf);
    c->rxlen = 0;
}

void rtsp_server_poll(rtsp_server_t *srv)
{
    rtsp_accept_new(srv);
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        if (srv->clients[i].tcp_fd >= 0) rtsp_service_client(srv, i);
    }
}

void rtsp_server_set_param_set(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len)
{
    if (nal_len == 0 || stream_id < 0 || stream_id >= RTSP_STREAM_COUNT) return;
    int type = nal[0] & 0x1F;
    if (type == 7 && nal_len <= sizeof(srv->sps[stream_id])) {
        memcpy(srv->sps[stream_id], nal, nal_len);
        srv->sps_len[stream_id] = nal_len;
    } else if (type == 8 && nal_len <= sizeof(srv->pps[stream_id])) {
        memcpy(srv->pps[stream_id], nal, nal_len);
        srv->pps_len[stream_id] = nal_len;
    }
}

#define RTP_MTU_PAYLOAD 1400

static void rtp_send_packet(rtsp_client_t *c, const uint8_t *payload, size_t len,
                             uint32_t ts, int marker)
{
    uint8_t pkt[12 + RTP_MTU_PAYLOAD];
    if (len > sizeof(pkt) - 12) len = sizeof(pkt) - 12;

    pkt[0] = 0x80;
    pkt[1] = (uint8_t)((marker ? 0x80 : 0x00) | 96);
    pkt[2] = (uint8_t)(c->seq >> 8);
    pkt[3] = (uint8_t)c->seq;
    pkt[4] = (uint8_t)(ts >> 24);
    pkt[5] = (uint8_t)(ts >> 16);
    pkt[6] = (uint8_t)(ts >> 8);
    pkt[7] = (uint8_t)ts;
    pkt[8] = (uint8_t)(c->ssrc >> 24);
    pkt[9] = (uint8_t)(c->ssrc >> 16);
    pkt[10] = (uint8_t)(c->ssrc >> 8);
    pkt[11] = (uint8_t)c->ssrc;
    memcpy(pkt + 12, payload, len);
    c->seq++;
    sendto(c->rtp_fd, pkt, 12 + len, 0, (struct sockaddr *)&c->rtp_addr, sizeof(c->rtp_addr));
}

void rtsp_server_send_nal(rtsp_server_t *srv, int stream_id, const uint8_t *nal, size_t nal_len,
                           uint32_t rtp_ts, int is_last_in_frame)
{
    if (nal_len == 0) return;

    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) {
        rtsp_client_t *c = &srv->clients[i];
        if (c->tcp_fd < 0 || !c->playing || c->rtp_fd < 0 || c->stream_id != stream_id) continue;

        if (nal_len <= RTP_MTU_PAYLOAD) {
            rtp_send_packet(c, nal, nal_len, rtp_ts, is_last_in_frame);
            continue;
        }

        /* RFC 6184 FU-A fragmentation for NALs above one RTP payload. */
        uint8_t nal_hdr = nal[0];
        uint8_t nal_type = nal_hdr & 0x1F;
        uint8_t nri = nal_hdr & 0x60;
        size_t off = 1;
        int first = 1;
        while (off < nal_len) {
            size_t chunk = nal_len - off;
            if (chunk > RTP_MTU_PAYLOAD - 2) chunk = RTP_MTU_PAYLOAD - 2;
            int last = (off + chunk >= nal_len);
            uint8_t buf[RTP_MTU_PAYLOAD];
            buf[0] = (uint8_t)(0x1C | nri); /* FU indicator: type 28 (FU-A) */
            buf[1] = (uint8_t)((first ? 0x80 : 0) | (last ? 0x40 : 0) | nal_type);
            memcpy(buf + 2, nal + off, chunk);
            rtp_send_packet(c, buf, chunk + 2, rtp_ts, last && is_last_in_frame);
            off += chunk;
            first = 0;
        }
    }
}

void rtsp_server_close(rtsp_server_t *srv)
{
    for (int i = 0; i < RTSP_MAX_CLIENTS; i++) rtsp_client_reset(&srv->clients[i]);
    if (srv->listen_fd >= 0) close(srv->listen_fd);
}
