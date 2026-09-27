#!/usr/bin/env bash
# End-to-end Jellyfin tests against tests/jellyfin_mock.py, for an old (10.8) and a
# current (10.10) server API. Usage: run-jellyfin-tests.sh <binary> <media-dir> <out-dir>
set -uo pipefail
BIN=$(readlink -f "$1"); M=$(readlink -f "$2"); O=$(mkdir -p "$3" && readlink -f "$3")
HERE=$(cd "$(dirname "$0")/.." && pwd)
rc=0
for VER in 10.8.13 10.10.3; do
  D="$O/jellyfin-$VER"; rm -rf "$D"; mkdir -p "$D"
  CFG=$(mktemp -d); PORT=$((18000 + RANDOM % 900))
  python3 "$HERE/tests/jellyfin_mock.py" --media "$M" --port $PORT --version $VER --log "$D/requests.jsonl" > "$D/mock.log" 2>&1 &
  MOCK=$!; sleep 1
  for t in jellyfin jellyfin-restore; do
    sed -e "s#@JF@#http://127.0.0.1:$PORT#g" "$HERE/tests/automation/$t.txt" > "$D/$t.txt"
    XDG_CONFIG_HOME="$CFG" timeout 200 "$BIN" --automation "$D/$t.txt" --automation-log "$D/$t.json" > "$D/$t.log" 2>&1
  done
  kill $MOCK 2>/dev/null
  python3 "$HERE/scripts/check-jellyfin.py" "$D" "$VER" "$CFG/CRTPlayer/CRTPlayer/jellyfin.json" | tee "$D/checks.txt" || rc=1
  rm -rf "$CFG"
done
exit $rc
