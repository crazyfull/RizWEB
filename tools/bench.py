#!/usr/bin/env python3
"""Tiny keep-alive HTTP load generator (no dependencies).

    python3 tools/bench.py [host] [port] [path] [connections] [seconds]

For serious numbers use wrk (wrk -t2 -c64 -d10s http://host:port/path); this script only
exists so the repository can be benchmarked on machines without extra tools. A Python client
is the bottleneck on fast servers, so treat its output as a lower bound.
"""
import asyncio, sys, time

host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
port = int(sys.argv[2]) if len(sys.argv) > 2 else 8080
path = sys.argv[3] if len(sys.argv) > 3 else "/ip"
conns = int(sys.argv[4]) if len(sys.argv) > 4 else 32
seconds = float(sys.argv[5]) if len(sys.argv) > 5 else 5.0
request = f"GET {path} HTTP/1.1\r\nHost: {host}\r\n\r\n".encode()


async def worker(deadline, counter):
    reader, writer = await asyncio.open_connection(host, port)
    while time.perf_counter() < deadline:
        writer.write(request)
        head = await reader.readuntil(b"\r\n\r\n")
        length = 0
        for line in head.split(b"\r\n"):
            if line.lower().startswith(b"content-length:"):
                length = int(line.split(b":")[1])
        if length:
            await reader.readexactly(length)
        counter[0] += 1
    writer.close()


async def main():
    counter = [0]
    start = time.perf_counter()
    await asyncio.gather(*[worker(start + seconds, counter) for _ in range(conns)])
    elapsed = time.perf_counter() - start
    print(f"{counter[0]} requests in {elapsed:.2f}s = {counter[0] / elapsed:.0f} req/s ({conns} connections)")


asyncio.run(main())
