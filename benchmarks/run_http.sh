#!/usr/bin/env bash
set -euo pipefail

command -v cmake >/dev/null || { echo "cmake is required" >&2; exit 1; }
command -v wrk >/dev/null || { echo "wrk is required" >&2; exit 1; }

build_dir="${BUILD_DIR:-build-benchmark}"
connections="${CONNECTIONS:-512}"
threads="${THREADS:-4}"
duration="${DURATION:-30s}"

cmake -S . -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
  -DSTTNET_BUILD_EXAMPLE=OFF -DSTTNET_BUILD_TESTS=OFF -DSTTNET_BUILD_BENCHMARK=ON
cmake --build "$build_dir" --parallel

"$build_dir/sttnet_http_benchmark" &
server_pid=$!
trap 'kill -TERM "$server_pid" 2>/dev/null || true; wait "$server_pid" 2>/dev/null || true' EXIT

for _ in {1..50}; do
  if curl --silent --fail http://127.0.0.1:8080/ping >/dev/null; then
    break
  fi
  sleep 0.1
done

wrk -t"$threads" -c"$connections" -d"$duration" --latency http://127.0.0.1:8080/ping
