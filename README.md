# Netlink-Agent

`Netlink-Agent` is a Linux network state monitoring and event collection agent built on top of `Netlink + epoll`. It subscribes to kernel `NETLINK_ROUTE` events, maintains a user-space network state model, and exposes interface state through a Unix Domain Socket CLI.

The goal is to evolve this project from a simple Netlink demo into an infrastructure-style Linux network monitoring agent: event-driven, low-overhead, observable, deployable, and extensible.

## Core Capabilities

- **Event-driven runtime**: uses `epoll` to handle Netlink and CLI sockets in one event loop.
- **Kernel event subscription**: listens for link, IPv4/IPv6 address, and IPv4/IPv6 route events.
- **User-space state model**: maintains interface status, counters, and address lists.
- **Lightweight query interface**: exposes a Unix Domain Socket CLI without requiring an HTTP dependency.
- **Metrics and alert foundation**: periodically refreshes RX/TX counters and emits basic warning logs.
- **Engineering foundation**: includes `Makefile`, sample config, and a systemd service template.

## Architecture Overview

```text
Linux Kernel
    |
    | NETLINK_ROUTE events
    v
+------------------+
|  Netlink Socket  |
+------------------+
          |
          v
+------------------+
| epoll event loop |
+------------------+
   |            |
   |            +----------------+
   |                             |
   v                             v
Netlink parser              Unix Socket CLI
   |                             |
   v                             v
+------------------+       query snapshot
|   State Model    | <----------------+
| iface/address    |
| stats counters   |
+------------------+
   |            |
   v            v
metrics      alert logs
```

See `ARCHITECTURE.md` for more details.

## Project Layout

```text
Netlink-Agent/
├── conf/
│   └── nlagent.conf          # sample config; full config loading is planned
├── src/
│   ├── main.c                # entry point, initialization, epoll loop
│   ├── netlink.c/.h          # Netlink socket and event parsing
│   ├── parser.c/.h           # user-space interface state model
│   ├── metrics.c/.h          # periodic metrics refresh entry
│   ├── alert.c/.h            # basic alert checks
│   ├── cli.c/.h              # Unix Socket CLI
│   └── logger.c/.h           # logging helpers
├── systemd/
│   └── nlagent.service       # systemd service template
├── Makefile
├── README.md
├── README.zh-CN.md
└── LICENSE
```

## Quick Start

### Build

```bash
make
```

Output:

```text
build/nlagent
```

### Run

`Netlink-Agent` reads Linux network state, so running with root privileges is recommended:

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

| Command | Description |
|---|---|
| `show interfaces` | Show all interface states, counters, and addresses |
| `list` | Alias of `show interfaces` |
| `help` | Show help |
| `quit` / `exit` | Close the current connection |

Example:

```bash
$ nc -U /tmp/nlagent.sock
=== Netlink Agent CLI ===
Available commands:
  show interfaces, list - Display interface status
  help - Show this help message
  quit, exit - Close connection

> show interfaces
=== Network Interfaces (2) ===
Interface: eth0
  Index: 2, Status: UP
  Counters: RX=3534918288 TX=2293304849 RX_ERR=0 TX_ERR=0
  Addresses (2):
    [1] 10.4.4.10/24 (IPv4)
    [2] fe80::1/64 (IPv6)

Interface: lo
  Index: 1, Status: UP
  Counters: RX=147969624 TX=147969624 RX_ERR=0 TX_ERR=0
  Addresses (2):
    [1] 127.0.0.1/8 (IPv4)
    [2] ::1/128 (IPv6)
```

## Technical Highlights

### 1. Netlink-driven event collection

The agent listens to `NETLINK_ROUTE` directly instead of periodically shelling out to `ip addr`, reducing latency and overhead.

### 2. epoll-based event loop

The runtime handles Netlink and CLI events in a single `epoll` loop, which is a better fit for lightweight system agents than one-thread-per-connection designs.

### 3. User-space state model

The `parser` module acts as the central state model for interface state, addresses, and counters. CLI, metrics, and alert logic all read from this state model.

### 4. Snapshot-based CLI query

CLI output is generated from a copied state snapshot, reducing coupling between query logic and the underlying linked-list state.

## Current Boundaries

Some foundations already exist but still need further engineering work:

- `conf/nlagent.conf` is currently a sample config; full config loading is planned.
- `systemd/nlagent.service` and `make install` need path alignment.
- Netlink, CLI, and state cleanup paths can be further hardened.
- Runtime metrics, JSON output, Prometheus exporter, and queue-based decoupling are planned improvements.

## Roadmap

The project should go deeper around performance, observability, and engineering stability:

1. Add graceful shutdown: close fds, unlink socket, and release state.
2. Add config loading with `-c /path/to/nlagent.conf`.
3. Align `make install` with the systemd service template.
4. Harden non-blocking `epoll` handling: drain loops, accept/read loops, and error paths.
5. Clarify state-model locking boundaries and keep `parser` as the SSOT.
6. Handle interface lifecycle correctly: `RTM_NEWLINK` update/create, `RTM_DELLINK` delete.
7. Add runtime metrics and expose them via CLI `show metrics`.
8. Extend CLI with `show interface <name>` and JSON output.
9. Introduce a bounded ring buffer to decouple Netlink receiving from state updates.
10. Add stress tests, sanitizer, valgrind, and CI.

## License

This project is licensed under the MIT License. See `LICENSE` for details.
