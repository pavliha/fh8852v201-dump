#!/bin/sh
set -eu

HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
OUT=$(mktemp -d)
NAME=rtsp-replay

docker rm -f "$NAME" >/dev/null 2>&1 || true
docker run -d --name "$NAME" -v "$REPO":/repo:ro gcc:14 sh -c '
gcc -O2 -Wall -Wextra -Werror -o /tmp/replay /repo/tools/majestic-oracle/replay.c /repo/majestic/rtsp_server.c &&
exec /tmp/replay /repo/tools/majestic-oracle/fixtures/input_1080p25_gop50.h264' >/dev/null
for _ in $(seq 1 120); do
    docker logs "$NAME" 2>&1 | grep -q 'RTSP on' && break
    [ "$(docker inspect -f '{{.State.Running}}' "$NAME")" = true ] || { docker logs "$NAME"; exit 1; }
    sleep 1
done
docker run --rm --network "container:$NAME" -v "$HERE":/w:ro -v "$OUT":/out python:3.12-slim \
    python3 /w/capture.py /out/ours.json
docker rm -f "$NAME" >/dev/null
python3 "$HERE/compare.py" "$HERE/fixtures/majestic.json" "$OUT/ours.json"
