import argparse
import base64
import json
import socket
import struct
import time

AUTH = 'Basic ' + base64.b64encode(b'root:12345').decode()


class Conn:
    def __init__(self, host, port):
        self.s = socket.create_connection((host, port), timeout=10)
        self.buf = b''
        self.packets = []

    def fill(self):
        chunk = self.s.recv(65536)
        if not chunk:
            raise EOFError
        self.buf += chunk

    def next_message(self):
        while True:
            if self.buf[:1] == b'$':
                while len(self.buf) < 4:
                    self.fill()
                n = struct.unpack('>H', self.buf[2:4])[0]
                while len(self.buf) < 4 + n:
                    self.fill()
                pkt = {'channel': self.buf[1], 'hex': self.buf[4:4 + n].hex()}
                self.buf = self.buf[4 + n:]
                return 'rtp', pkt
            if self.buf.startswith(b'RTSP/'):
                while b'\r\n\r\n' not in self.buf:
                    self.fill()
                head, rest = self.buf.split(b'\r\n\r\n', 1)
                clen = next((int(l.split(':')[1]) for l in head.decode('latin1').split('\r\n')
                             if l.lower().startswith('content-length:')), 0)
                while len(rest) < clen:
                    self.fill()
                    head, rest = self.buf.split(b'\r\n\r\n', 1)
                self.buf = rest[clen:]
                return 'rtsp', (head + b'\r\n\r\n' + rest[:clen]).decode('latin1')
            self.fill()

    def request(self, text, log):
        self.s.sendall(text.encode())
        while True:
            kind, m = self.next_message()
            if kind == 'rtsp':
                log.append({'request': text, 'response': m})
                return m
            self.packets.append(m)


def header(msg, name):
    return next((l.split(':', 1)[1].strip() for l in msg.split('\r\n') if l.lower().startswith(name + ':')), None)


def session(host, port, transport, packet_target):
    c = Conn(host, port)
    log = []
    url = f'rtsp://{host}:{port}/stream=0'
    h = f'Authorization: {AUTH}\r\nUser-Agent: oracle\r\n'
    c.request(f'OPTIONS {url} RTSP/1.0\r\nCSeq: 1\r\n{h}\r\n', log)
    d = c.request(f'DESCRIBE {url} RTSP/1.0\r\nCSeq: 2\r\n{h}Accept: application/sdp\r\n\r\n', log)
    control = [l[len('a=control:'):] for l in d.split('\r\n') if l.startswith('a=control:')]
    track = (header(d, 'content-base') or url).rstrip('/') + '/' + control[0]
    if transport == 'tcp':
        tr = 'RTP/AVP/TCP;unicast;interleaved=0-1'
    else:
        rtp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rtp.bind(('0.0.0.0', 45000))
        rtp.settimeout(5)
        rtcp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        rtcp.bind(('0.0.0.0', 45001))
        rtcp.settimeout(3)
        tr = 'RTP/AVP;unicast;client_port=45000-45001'
    r = c.request(f'SETUP {track} RTSP/1.0\r\nCSeq: 3\r\n{h}Transport: {tr}\r\n\r\n', log)
    sess = header(r, 'session').split(';')[0]
    c.request(f'PLAY {url} RTSP/1.0\r\nCSeq: 4\r\n{h}Session: {sess}\r\nRange: npt=0.000-\r\n\r\n', log)
    t0 = time.time()
    while len(c.packets) < packet_target and time.time() - t0 < 60:
        if transport == 'tcp':
            kind, m = c.next_message()
            if kind == 'rtp':
                c.packets.append(m)
        else:
            data, _ = rtp.recvfrom(65536)
            c.packets.append({'channel': 0, 'hex': data.hex()})
    c.request(f'GET_PARAMETER {url} RTSP/1.0\r\nCSeq: 5\r\n{h}Session: {sess}\r\n\r\n', log)
    c.request(f'TEARDOWN {url} RTSP/1.0\r\nCSeq: 6\r\n{h}Session: {sess}\r\n\r\n', log)
    if transport == 'udp':
        try:
            while True:
                data, _ = rtcp.recvfrom(65536)
                c.packets.append({'channel': 1, 'hex': data.hex()})
        except OSError:
            pass
    return {'transport': transport, 'exchanges': log, 'packets': c.packets}


def two_tracks(host, port, seconds):
    c = Conn(host, port)
    log = []
    url = f'rtsp://{host}:{port}/stream=0'
    h = f'Authorization: {AUTH}\r\nUser-Agent: oracle\r\n'
    c.request(f'DESCRIBE {url} RTSP/1.0\r\nCSeq: 1\r\n{h}\r\n', log)
    r = c.request(f'SETUP {url}/video RTSP/1.0\r\nCSeq: 2\r\n{h}Transport: RTP/AVP/TCP;unicast;interleaved=0-1\r\n\r\n', log)
    sess = header(r, 'session').split(';')[0]
    c.request(f'SETUP {url}/metadata RTSP/1.0\r\nCSeq: 3\r\n{h}Session: {sess}\r\nTransport: RTP/AVP/TCP;unicast;interleaved=2-3\r\n\r\n', log)
    c.request(f'PLAY {url} RTSP/1.0\r\nCSeq: 4\r\n{h}Session: {sess}\r\n\r\n', log)
    t0 = time.time()
    while time.time() - t0 < seconds:
        kind, m = c.next_message()
        if kind == 'rtp':
            c.packets.append(m)
    c.request(f'TEARDOWN {url} RTSP/1.0\r\nCSeq: 5\r\n{h}Session: {sess}\r\n\r\n', log)
    return {'exchanges': log, 'packets': c.packets}


def single(host, port, text):
    c = Conn(host, port)
    log = []
    c.request(text, log)
    return log[0]


def error_cases(host, port):
    u = f'rtsp://{host}:{port}'
    bad = base64.b64encode(b'root:bad').decode()
    requests = [
        f'OPTIONS {u}/stream=0 RTSP/1.0\r\nCSeq: 1\r\nUser-Agent: oracle\r\n\r\n',
        f'DESCRIBE {u}/stream=9 RTSP/1.0\r\nCSeq: 1\r\nAuthorization: {AUTH}\r\nUser-Agent: oracle\r\n\r\n',
        f'DESCRIBE {u}/foo RTSP/1.0\r\nCSeq: 1\r\nAuthorization: {AUTH}\r\nUser-Agent: oracle\r\n\r\n',
        f'PLAY {u}/stream=0 RTSP/1.0\r\nCSeq: 7\r\nAuthorization: {AUTH}\r\nSession: 12345\r\nUser-Agent: oracle\r\n\r\n',
        f'DESCRIBE {u}/stream=0 RTSP/1.0\r\nCSeq: 1\r\nAuthorization: Basic {bad}\r\nUser-Agent: oracle\r\n\r\n',
        f'PAUSE {u}/stream=0 RTSP/1.0\r\nCSeq: 8\r\nUser-Agent: oracle\r\n\r\n',
        f'SET_PARAMETER {u}/stream=0 RTSP/1.0\r\nCSeq: 9\r\nUser-Agent: oracle\r\n\r\n',
        f'ANNOUNCE {u}/stream=0 RTSP/1.0\r\nCSeq: 10\r\nUser-Agent: oracle\r\n\r\n',
        f'GET_PARAMETER {u}/stream=0 RTSP/1.0\r\nCSeq: 11\r\nUser-Agent: oracle\r\n\r\n',
        f'TEARDOWN {u}/stream=0 RTSP/1.0\r\nCSeq: 12\r\nUser-Agent: oracle\r\n\r\n',
        f'SETUP {u}/stream=0/video RTSP/1.0\r\nCSeq: 13\r\nTransport: RTP/AVP/TCP;unicast;interleaved=4-5\r\nUser-Agent: oracle\r\n\r\n',
        f'SETUP {u}/stream=1/video RTSP/1.0\r\nCSeq: 14\r\nTransport: RTP/AVP/TCP;unicast;interleaved=0-1\r\nUser-Agent: oracle\r\n\r\n',
        f'SETUP {u}/stream=0/video RTSP/1.0\r\nCSeq: 15\r\nTransport: RTP/AVP;multicast\r\nUser-Agent: oracle\r\n\r\n',
    ]
    return [single(host, port, r) for r in requests]


if __name__ == '__main__':
    ap = argparse.ArgumentParser()
    ap.add_argument('out')
    ap.add_argument('--host', default='127.0.0.1')
    ap.add_argument('--port', type=int, default=554)
    ap.add_argument('--packets', type=int, default=400)
    a = ap.parse_args()
    out = {
        'errors': error_cases(a.host, a.port),
        'sessions': [session(a.host, a.port, 'tcp', a.packets), session(a.host, a.port, 'udp', a.packets)],
        'two_tracks': two_tracks(a.host, a.port, 12),
    }
    json.dump(out, open(a.out, 'w'), indent=1)
    print(a.out, [len(s['packets']) for s in out['sessions']], len(out['two_tracks']['packets']))
