#!/usr/bin/env bash
# RizWEB vs nginx on identical static files (needs: wrk, nginx, a built rizweb binary).
#   tools/benchVsNginx.sh /path/to/rizweb
set -euo pipefail
RIZWEB=$(realpath "${1:?usage: $0 /path/to/rizweb}")
WORK=$(mktemp -d); chmod 755 "$WORK"; trap 'kill $(jobs -p) 2>/dev/null || true; pkill -x rizweb 2>/dev/null || true; nginx -c "$WORK/nginx.conf" -s stop 2>/dev/null || true' EXIT
mkdir -p "$WORK/www" "$WORK/rizweb"
python3 - "$WORK/www" <<'PY'
import os, sys
d = sys.argv[1]
open(d + "/index.html", "w").write("<!doctype html><html><body>" + "hello world " * 40 + "</body></html>")
open(d + "/100k.bin", "wb").write(os.urandom(100 * 1024))
open(d + "/1m.bin", "wb").write(os.urandom(1024 * 1024))
PY
cat > "$WORK/nginx.conf" <<CONF
worker_processes 2; pid $WORK/nginx.pid; error_log $WORK/err.log crit;
events { worker_connections 4096; }
http { access_log off; sendfile on; tcp_nopush on; keepalive_timeout 20; keepalive_requests 1000000;
  server_tokens off; gzip off; open_file_cache max=100 inactive=60s;
  client_body_temp_path $WORK/b; proxy_temp_path $WORK/p; fastcgi_temp_path $WORK/f; uwsgi_temp_path $WORK/u; scgi_temp_path $WORK/s;
  server { listen 8081; root $WORK/www; } }
CONF
cp "$RIZWEB" "$WORK/rizweb/rizweb"; cp -r "$(dirname "$0")/../resource" "$WORK/rizweb/"
(cd "$WORK/rizweb" && timeout 2 ./rizweb >/dev/null 2>&1 || true)          # creates settings.ini
sed -i "s#LocatedRoot = .*#LocatedRoot = $WORK/www#" "$WORK/rizweb/settings.ini"
(cd "$WORK/rizweb" && exec ./rizweb >/dev/null 2>&1) &
nginx -c "$WORK/nginx.conf"; sleep 1
for p in 8080 8081; do [ "$(curl -s -o /dev/null -w %{http_code} http://127.0.0.1:$p/index.html)" = 200 ] || { echo "server on port $p does not answer 200 - aborting"; exit 1; }; done
for f in index.html 100k.bin 1m.bin; do for c in 64 512; do for p in 8080 8081; do
  printf '%-5s %-10s c=%-4s ' "$([ $p = 8080 ] && echo rizweb || echo nginx)" "$f" "$c"
  wrk -t2 -c$c -d8s --latency "http://127.0.0.1:$p/$f" | awk '/Requests\/sec/{r=$2}/Transfer\/sec/{t=$2}/ 99%/{p=$2}/Non-2xx/{e=" NON-2XX!"}END{print "req/s=" r " xfer=" t " p99=" p e}'
done; done; done
