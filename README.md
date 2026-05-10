# epoll-http-server
A performance focused epoll based HTTP server written using the linux socket API.

## Performance Benchmarks

- **Request**: `GET /index.html`
- **Response**: Static HTML file (~1500 bytes)
- **CPU**: Intel i5-13420H (13th Gen)
- **Compiler**: Clang (O3)

| Architecture | Internal (req/sec) | End to End (req/sec) | Description |
| :--- | :--- | :--- | :--- |
| **Blocking** | ~36,000 | ~13,000 | Single threaded blocking accept/read/write. |
| **Epoll** | *TBD* | *TBD* | *TBD* |

- **Internal:** Server side request processing only (excludes accept/close)
- **End to End:** Full request lifecycle including connect(), kernel TCP setup, response transfer, and close()

*Note: All benchmarks were run over localhost loopback*
