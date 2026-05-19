# epoll-http-server
A performance focused epoll based HTTP server written using the linux socket API.

## Performance Benchmarks

- **Benchmark command** : `wrk -t4 -c10000 -d10s http://127.0.0.1:8080/`
- **Request**: `GET /index.html`
- **Response**: Static HTML file (~1500 bytes)
- **CPU**: Intel i5-13420H (13th Gen)
- **Compiler**: Clang (O3)

| Architecture | req/sec | Description |
| :--- | :--- | :--- |
| **Blocking** | ~15,000 | Single threaded blocking accept/read/write. |
| **Epoll** | ~34,000 | Single threaded event loop utlizing non blocking I/O multiplexing |
