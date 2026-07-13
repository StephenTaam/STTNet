#!/usr/bin/env bash
set -u

binary=${1:-./build/sttnet_example}
"$binary" &
server_pid=$!

force_cleanup() {
  if kill -0 "$server_pid" 2>/dev/null; then
    kill -KILL "$server_pid" 2>/dev/null || true
  fi
}
trap force_cleanup EXIT

ready=false
for _ in {1..50}; do
  if response=$(curl --silent --fail http://127.0.0.1:8080/ping 2>/dev/null); then
    if [[ $response == pong ]]; then
      ready=true
      break
    fi
  fi
  sleep 0.1
done

if [[ $ready != true ]]; then
  echo "HTTP server did not become ready" >&2
  exit 1
fi

kill -TERM "$server_pid"
exited=false
for _ in {1..100}; do
  if ! kill -0 "$server_pid" 2>/dev/null; then
    exited=true
    break
  fi
  if [[ -r /proc/$server_pid/stat ]] && [[ $(awk '{print $3}' "/proc/$server_pid/stat") == Z ]]; then
    exited=true
    break
  fi
  sleep 0.1
done

if [[ $exited != true ]]; then
  echo "server ignored SIGTERM or graceful shutdown exceeded 10 seconds" >&2
  exit 1
fi

wait "$server_pid"
status=$?
trap - EXIT
if [[ $status -ne 0 ]]; then
  echo "server exited with status $status after SIGTERM" >&2
  exit 1
fi

echo "SIGTERM graceful shutdown passed"
