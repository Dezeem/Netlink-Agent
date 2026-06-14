# Netlink-Agent

`Netlink-Agent` is a Linux network monitoring daemon built on `Netlink + epoll`.
It subscribes to kernel `NETLINK_ROUTE` events, maintains a user-space network
state model (interfaces, addresses, routes), and exposes observability through
a Unix Domain Socket CLI and a Prometheus `/metrics` endpoint.

## Core Capabilities

- **Event-driven daemon** — `epoll_wait(-1)` with timerfd, netlink, prometheus, and unix socket as fd sources.
- **Kernel event subscription** — link, IPv4/IPv6 address, and IPv4/IPv6 route events.
- **SPSC worker queue** — lock-free ring buffer decouples netlink receive from state updates with backpressure and drop tracking.
- **User-space state model (SSOT)** — interface state, full `rtnl_link_stats64` counters, address lists, and route table.
- **Prometheus exporter** — HTTP `/metrics` on `:9100`, standard format with HELP/TYPE annotations.
- **Atomic config hot-reload** — double-buffer `config_t` via `SIGHUP` or CLI `reload`, sourced from environment variables.
- **Perf benchmarks** — automated `perf stat` throughput profiling at three intensity levels.
- **ASan + UBSan build target** — `make asan` catches misaligned access, leaks, and UB at dev time.

## Architecture

```text
                       Linux Kernel
                           |
                    NETLINK_ROUTE events
                           |
                    +------v------+
                    |  epoll fd   |  timerfd (5s)
                    |  sources    |  Prometheus :9100
                    +-------------+  Unix Socket CLI
                     |            |
        recvmsg + push to queue   |
                     |            |
              +------v------+     |
              | SPSC Ring   |     |
              | Buffer (256)|     |
              +------+------+     |
                     |            |
                pop + dispatch    |
                     |            |
              +------v------+     |
              | Worker      |     |
              | Thread      |     |
              +------+------+     |
                     |            |
              +------v------------v------+
              |      Parser (SSOT)       |
              |  iface_list  route_list  |
              |  rwlock protection       |
              +--------------------------+
                |       |        |
                v       v        v
            metrics   alert    CLI query
```

### Data Model

```text
iface_info (linked list)
  ├─ ifname[IFNAMSIZ], ifindex, up
  ├─ stats: rtnl_link_stats64 (rx/tx bytes, errors, drops, ...)
  ├─ addrs[MAX_ADDR_PER_IF]: family, prefixlen, addr[]
  └─ next

route_info (linked list)
  ├─ dst[], prefixlen, family
  ├─ gateway[], oif (output interface index)
  ├─ rtm_type (unicast/local/...), rtm_protocol (kernel/boot/static/dhcp)
  └─ next
```

### Event Flow (Sequence)

```text
epoll_wait(-1)             worker_thread              parser
    │                          │                        │
    ├─ netlink readable        │                        │
    │  └─ recvmsg → nlh        │                        │
    │     └─ nl_event_alloc()  │                        │
    │        └─ push(queue) ──►│                        │
    │                          ├─ pop(queue)            │
    │                          ├─ dispatch(event)       │
    │                          │  ├─ handle_link_msg ──►│ upsert / delete
    │                          │  ├─ handle_addr_msg ──►│ add / del addr
    │                          │  └─ handle_route_msg ─►│ route_upsert / delete
    │                          ├─ record queue depth    │
    │                          └─ free(event)           │
    │                                                   │
    ├─ timerfd (5s)                                     │
    │  └─ metrics_poll_once() ─────────────────────────►│ update stats
    │  └─ alert_check_cycle() ─────────────────────────►│ read + warn
    │                                                   │
    ├─ prometheus fd                                    │
    │  └─ accept → snapshot → write → close             │
    │                                                   │
    └─ unix socket                                      │
       └─ CLI accept / command handle                   │
```

## Project Layout

```text
Netlink-Agent/
├── benchmark/
│   ├── perf_report.md         # latest perf benchmark report
│   └── stress_log.md          # performance baseline
├── tests/
│   ├── stress_test.sh         # parameterized stress test (veth / addr / CLI)
│   └── perf_bench.sh          # 3-tier perf stat benchmark
├── src/
│   ├── main.c                 # entry, init, epoll loop, signal handling
│   ├── netlink.c/.h           # netlink socket, event parsing, dispatch
│   ├── parser.c/.h            # SSOT state model (iface + addr + route)
│   ├── event_queue.c/.h       # SPSC lock-free ring buffer
│   ├── event_worker.c/.h      # worker thread (pop → dispatch → free)
│   ├── metrics.c/.h           # periodic stats refresh trigger
│   ├── alert.c/.h             # error / traffic threshold checks
│   ├── cli.c/.h               # Unix Domain Socket CLI
│   ├── prom.c/.h              # Prometheus HTTP exporter (:9100)
│   ├── config.c/.h            # atomic double-buffer config (SIGHUP reload)
│   ├── runtime_metrics.c/.h   # event / depth / drop / worker counters
│   └── logger.c/.h            # timestamped stdout logger
├── conf/
│   └── nlagent.conf           # sample config
├── systemd/
│   └── nlagent.service        # systemd unit template
├── Makefile
└── LICENSE
```

## Quick Start

### Build

```bash
make          # release (-O2)
make debug    # debug (-O0 -DDEBUG)
make asan     # AddressSanitizer + UBSan
```

### Run

```bash
sudo ./build/nlagent
```

or:

```bash
make run
```

### Query via CLI

```bash
nc -U /tmp/nlagent.sock
```

Available commands:

| Command                    | Description                         |
|---|-------------------------------------|
| `show interfaces`, `list`  | All interface states, counters, addresses |
| `show interface <ifname>`  | Single interface detail             |
| `show routes`              | Route table (dst, proto, gateway, oif) |
| `show metrics`             | Runtime counters and queue depth    |
| `reload`                   | Hot-reload config from environment  |
| `help`                     | Show help                           |
| `quit`, `exit`             | Close connection                    |

Example — routes:

```bash
$ nc -U /tmp/nlagent.sock
> show routes
=== Route Table (13 entries) ===
Destination          Proto   Gateway          OIF
ff00::/8             kernel  *                eth0
fe80::/64            kernel  *                eth0
::1/128              kernel  *                lo
192.168.16.0/20      kernel  *                eth0
0.0.0.0/0            dhcp    192.168.16.1     eth0
...
```

Example — metrics:

```bash
> show metrics
=== Runtime Metrics ===
netlink_events_total 4048
  link_events 1508
  addr_events 141
  route_events 2399
netlink_dropped_total 487
worker_events_total 4048
queue_depth_current 0
queue_depth_max 255
backpressure_pct 12.0%
...
```

### Prometheus

```bash
curl http://localhost:9100/metrics
```

### Config Hot-Reload

```bash
# Via CLI
echo reload | nc -U /tmp/nlagent.sock

# Via signal
sudo kill -HUP $(pgrep nlagent)

# Override thresholds at reload time
NLAGENT_ERR_THRESHOLD=5 NLAGENT_TRAFFIC_MBPS=20 systemctl reload nlagent
```

### Stress Test

```bash
# Interactive stress (15s, 200 veth pairs, 50 addrs, 30 CLI clients)
sudo bash tests/stress_test.sh -d 15 -v 200 -a 50 -c 30

# Perf benchmark (3-tier automated profiling)
sudo bash tests/stress_test.sh -p
```

## Technical Highlights

- **Pure event-driven epoll** — `epoll_wait(-1)` blocks until a real fd event fires. No polling, no timeout hacks, every I/O and timer is an fd.
- **SPSC lock-free queue** — ring buffer decouples `recvmsg` from handler dispatch. 256 slots, atomic head/tail, backpressure with drop counting visible in metrics.
- **Unified state model (SSOT)** — `parser.c` owns interface and route state under `pthread_rwlock_t`. Every other module reads through the parser API.
- **Atomic config reload** — double-buffer `config_t` swapped via `_Atomic(config_t *)`. Zero-downtime reload, safe concurrent reads in alert and dispatch paths.
- **Observability built in** — Prometheus endpoint, runtime metrics, structured log levels (INFO/WARN/ERROR), and automated `perf stat` profiling.
- **Sanitizer CI-ready** — `make asan` compiles with AddressSanitizer + UBSan. Validated zero-leak startup-shutdown cycle under `valgrind`.

## Current Boundaries

- Config is sourced from environment variables; file-based parser is a planned addition.
- The route table grows unbounded under heavy churn (no LRU eviction).
- Alert thresholds are reactive-only; no hysteresis or rate-limiting on warnings.
- ARM alignment safety relies on `memcpy` in the netlink-stats path (verified by UBSan).

## Roadmap

1. File-based config as primary source, env vars as override (`/etc/nlagent.conf`).
2. Route-table LRU eviction for high-churn environments.
3. Alert deduplication and hysteresis.
4. `json` output mode for CLI.
5. `make install` path alignment with systemd unit.
6. CI workflow (build + asan + stress + valgrind).

## License

MIT License. See `LICENSE` for details.
