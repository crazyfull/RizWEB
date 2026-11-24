#!/usr/bin/env bash
# Dynamic short responses: RizWEB handler (/ip) vs nginx "return 200 ..." (no disk, no proxy).
# Usage: tools/benchDynamic.sh path/to/rizweb      (needs wrk and nginx)
set -euo pipefail
BIN=${1:?path to rizweb}
WORK=$(mktemp -d); chmod 755 "$WORK"; trap 'kill $(jobs -p) 2>/dev/null || true; nginx -c "$WORK/n.conf" -s stop 2>/dev/null || true; rm -rf "$WORK"' EXIT
mkdir -p "$WORK/www" "$WORK/app"; echo hi > "$WORK/www/index.html"
cp "$BIN" "$WORK/app/rizweb"; cp -r "$(dirname "$0")/../resource" "$WORK/app/resource"
( cd "$WORK/app" && timeout 2 ./rizweb >/dev/null 2>&1 || true )
sed -i "s#^LocatedRoot.*#LocatedRoot = $WORK/www#" "$WORK/app/settings.ini"
( cd "$WORK/app" && exec ./rizweb >/dev/null 2>&1 ) &
cat > "$WORK/n.conf" <<C
worker_processes 2; pid $WORK/n.pid; error_log $WORK/err.log;
events { worker_connections 8192; }
http { access_log off; client_body_temp_path $WORK/b; proxy_temp_path $WORK/p; fastcgi_temp_path $WORK/f; uwsgi_temp_path $WORK/u; scgi_temp_path $WORK/s;
  server { listen 8081; location = /ip { default_type text/plain; return 200 "your ip is 127.0.0.1\n"; } } }
C
nginx -c "$WORK/n.conf"; sleep 1
for p in 8080 8081; do [ "$(curl -s -o /dev/null -w %{http_code} http://127.0.0.1:$p/ip)" = 200 ] || { echo "port $p: /ip is not 200"; exit 1; }; done
for c in 64 512; do for p in 8080 8081; do
  printf "c=%-4s port=%s  " "$c" "$p"
  wrk -t2 -c$c -d8s --latency "http://127.0.0.1:$p/ip" | awk '/Requests\/sec/{r=$2}/ 99%/{l=$2}/Non-2xx/{e=" NON-2XX!"}END{print "req/s=" r " p99=" l e}'
done; done
