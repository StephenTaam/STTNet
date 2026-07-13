#!/usr/bin/env python3
"""Hold many deliberately slow HTTP readers open against the /large route."""

import os
import socket
import threading
import time


HOST = os.environ.get("HOST", "127.0.0.1")
PORT = int(os.environ.get("PORT", "8080"))
CLIENTS = int(os.environ.get("SLOW_CLIENTS", "128"))
HOLD_SECONDS = float(os.environ.get("HOLD_SECONDS", "30"))
READ_INTERVAL = float(os.environ.get("READ_INTERVAL", "0.2"))


def slow_reader() -> None:
    deadline = time.monotonic() + HOLD_SECONDS
    try:
        with socket.create_connection((HOST, PORT), timeout=5) as connection:
            connection.sendall(
                b"GET /large HTTP/1.1\r\n"
                b"Host: 127.0.0.1\r\n"
                b"Connection: close\r\n\r\n"
            )
            connection.settimeout(1)
            while time.monotonic() < deadline:
                try:
                    if not connection.recv(1):
                        return
                except socket.timeout:
                    pass
                time.sleep(READ_INTERVAL)
    except OSError:
        # Backpressure may intentionally evict a slow reader at the high-water mark.
        return


threads = [threading.Thread(target=slow_reader) for _ in range(CLIENTS)]
for thread in threads:
    thread.start()
for thread in threads:
    thread.join()
