#!/bin/bash
# High-concurrency run against an already running server (needs wrk and iproute2 `ss`).
#   tools/benchHighConnections.sh <port> <process-name> <connections> <path>
# Reports total requests, req/s, latency, established connections and server RSS. Raise `ulimit -n` first;
# every connection needs a descriptor on both sides (and one more per file being sent).
# $1 port $2 name $3 conns $4 path
ulimit -n 20000
( wrk -t2 -c$3 -d12s --latency http://127.0.0.1:$1/$4 > ${TMPDIR:-/tmp}/wrk.out 2>&1 ) &
sleep 7; rss=$(ps -C $2 -o rss= | awk '{s+=$1}END{print int(s/1024)}'); est=$(ss -tn state established "( sport = :$1 )" 2>/dev/null | tail -n +2 | wc -l)
wait
awk -v n="$2" -v c=$3 -v rss=$rss -v est=$est '/requests in/{req=$1}/Requests\/sec/{r=$2}/ 50%/{p50=$2}/ 99%/{p99=$2}/Socket errors/{e=$0}/Non-2xx/{e=e" "$0}END{printf "%-7s c=%-6s requests=%-9s req/s=%-10s p50=%-8s p99=%-8s established@7s=%-6s RSS=%sMB %s\n",n,c,req,r,p50,p99,est,rss,e}' ${TMPDIR:-/tmp}/wrk.out
