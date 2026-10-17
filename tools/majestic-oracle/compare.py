import json
import re
import struct
import sys

failures = []


def check(cond, what):
    if not cond:
        failures.append(what)
    return cond


def norm(text):
    text = re.sub(r'rtsp://[0-9.]+:', 'rtsp://<host>:', text)
    text = re.sub(r'Session: \d+', 'Session: <id>', text)
    text = re.sub(r'server_port=(\d+)-(\d+)',
                  lambda m: 'server_port=<p>-<p+1>' if int(m.group(2)) == int(m.group(1)) + 1 else m.group(0), text)
    return re.sub(r'UtcTime="[^"]*"', 'UtcTime="<utc>"', text)


def parse(hexs):
    b = bytes.fromhex(hexs)
    seq, ts, ssrc = struct.unpack('>HII', b[2:12])
    return {'b0': b[0], 'm': b[1] >> 7, 'pt': b[1] & 0x7f, 'seq': seq, 'ts': ts, 'ssrc': ssrc, 'payload': b[12:], 'raw': b}


def bye(ssrc):
    return bytes([0x81, 0xcb, 0x00, 0x05]) + struct.pack('>I', ssrc) + bytes([13]) + b'session ended' + b'\0\0'


def compare_exchanges(name, expected, actual):
    check(len(expected) == len(actual), f'{name}: {len(actual)} exchanges, majestic had {len(expected)}')
    for e, a in zip(expected, actual):
        req = e['request'].split('\r\n')[0]
        check(norm(e['response']) == norm(a['response']),
              f'{name}: {req}\n  majestic: {norm(e["response"])!r}\n  ours:     {norm(a["response"])!r}')


def access_units(packets):
    units = []
    for p in packets:
        if units and units[-1]['ts'] == p['ts']:
            units[-1]['packets'].append((p['m'], p['payload']))
        else:
            units.append({'ts': p['ts'], 'packets': [(p['m'], p['payload'])]})
    for u in units:
        u['key'] = tuple(u['packets'])
    return units


def check_stream(name, packets, pt):
    check(len({p['ssrc'] for p in packets}) == 1, f'{name}: SSRC changes within the session')
    check(all(p['b0'] == 0x80 and p['pt'] == pt for p in packets), f'{name}: unexpected V/P/X/CC or payload type')
    check([p['seq'] for p in packets] == list(range(len(packets))), f'{name}: sequence numbers do not run 0,1,2,...')


def compare_video(name, expected_pkts, actual_pkts):
    exp = [parse(p['hex']) for p in expected_pkts if p['channel'] == 0]
    act = [parse(p['hex']) for p in actual_pkts if p['channel'] == 0]
    check_stream(f'{name} majestic', exp, 96)
    check_stream(f'{name} ours', act, 96)
    check(act and act[0]['payload'][0] & 0x1f == 7, f'{name}: our session does not start with SPS')
    eu, au = access_units(exp), access_units(act)
    known = {u['key'] for u in eu}
    transitions = {(a['key'], b['key']): (b['ts'] - a['ts']) & 0xffffffff for a, b in zip(eu, eu[1:])}
    for i, u in enumerate(au[:-1]):
        if not check(u['key'] in known, f'{name}: frame {i} packetized differently from every majestic frame'):
            ref = next((x for x in eu if [p for _, p in x['packets']][0][:1] == [p for _, p in u['packets']][0][:1]), None)
            if ref:
                failures.append(f'  ours: {[(m, len(p)) for m, p in u["packets"]][:6]}\n  majestic: {[(m, len(p)) for m, p in ref["packets"]][:6]}')
            break
    steps = [((a['key'], b['key']), (b['ts'] - a['ts']) & 0xffffffff) for a, b in zip(au, au[1:-1])]
    for (key, step) in steps:
        if key in transitions:
            check(abs(step - transitions[key]) <= 1, f'{name}: timestamp step {step} vs majestic {transitions[key]}')
    for (k1, s1), (k2, s2) in zip(steps, steps[1:]):
        if k1 in transitions and k2 in transitions:
            check(s1 + s2 == transitions[k1] + transitions[k2], f'{name}: two-frame timestamp span differs from majestic')
    rtcp = [bytes.fromhex(p['hex']) for p in actual_pkts if p['channel'] == 1]
    check(rtcp == [bye(act[0]['ssrc'])], f'{name}: RTCP should be exactly one BYE, got {[r.hex() for r in rtcp]}')


def compare_metadata(expected, actual):
    exp = [parse(p['hex']) for p in expected['packets'] if p['channel'] == 2]
    act = [parse(p['hex']) for p in actual['packets'] if p['channel'] == 2]
    check_stream('metadata ours', act, 107)
    check(abs(len(exp) - len(act)) <= 1, f'metadata: {len(act)} packets, majestic sent {len(exp)}')
    check(all(p['m'] == 1 for p in act), 'metadata: marker bit not set')
    check({norm(p['payload'].decode()) for p in act} == {norm(p['payload'].decode()) for p in exp},
          'metadata: XML differs from majestic')
    steps = [(b['ts'] - a['ts']) & 0xffffffff for a, b in zip(act, act[1:])]
    check(all(85000 <= s <= 95000 for s in steps), f'metadata: timestamp steps {steps} are not ~1s at 90kHz')
    video = [parse(p['hex']) for p in actual['packets'] if p['channel'] == 0]
    tail = [(p['channel'], bytes.fromhex(p['hex'])) for p in actual['packets'] if p['channel'] in (1, 3)]
    check(tail == [(1, bye(video[0]['ssrc'])), (3, bye(act[0]['ssrc']))],
          f'two tracks: BYE packets should be video then metadata, got {[(c, b.hex()) for c, b in tail]}')


def main(expected_path, actual_path):
    expected = json.load(open(expected_path))
    actual = json.load(open(actual_path))
    compare_exchanges('errors', expected['errors'], actual['errors'])
    compare_exchanges('auth', expected['auth'], actual['auth'])
    names = ['tcp stream=0', 'udp stream=0', 'tcp stream=1']
    for name, e, a in zip(names, expected['sessions'], actual['sessions']):
        compare_exchanges(name, e['exchanges'], a['exchanges'])
        compare_video(name, e['packets'], a['packets'])
    for i, (e, a) in enumerate(zip(expected['concurrent'], actual['concurrent'])):
        compare_exchanges(f'concurrent {i}', e['exchanges'], a['exchanges'])
        compare_video(f'concurrent {i}', e['packets'], a['packets'])
    ssrcs = {parse(s['packets'][0]['hex'])['ssrc'] for s in actual['concurrent']}
    check(len(ssrcs) == 2, 'concurrent sessions share an SSRC')
    compare_exchanges('two tracks', expected['two_tracks']['exchanges'], actual['two_tracks']['exchanges'])
    compare_metadata(expected['two_tracks'], actual['two_tracks'])
    if failures:
        print('\n'.join(failures))
        print(f'FAIL: {len(failures)} difference(s) from majestic')
        return 1
    print('OK: RTSP/RTP output matches majestic byte for byte (random IDs, SSRCs, timestamp bases and wall-clock time excepted)')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1], sys.argv[2]))
