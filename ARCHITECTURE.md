# Netlink-Agent 架构说明

本文档描述 `Netlink-Agent` 的当前架构、模块边界和后续演进方向。

## 1. 项目定位

`Netlink-Agent` 的定位是：

> 基于 Netlink 的 Linux 网络状态监控与事件采集 Agent。

它不是简单打印 Netlink 事件的 demo，而是将内核网络事件稳定、低开销、可观测地转化为用户态网络状态模型，并通过 CLI、metrics 和日志对外暴露。

核心关键词：

- `event-driven`
- `epoll`
- `non-blocking`
- `low-overhead`
- `state model`
- `observable`
- `deployable`

## 2. 当前架构

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

## 3. 启动流程

入口位于 `src/main.c`。

```text
main()
├── 注册 SIGINT / SIGTERM
├── perform_initialization()
│   ├── init_iface_table()
│   └── update_all_iface_performance_data()
├── epoll_create1()
├── netlink_start(epfd)
├── cli_start(epfd)
└── epoll_wait() 主循环
```

初始化阶段会先建立接口状态表，再采集一轮统计数据。初始化完成后进入事件驱动运行阶段。

## 4. 运行流程

```text
epoll_wait()
├── Netlink fd 可读
│   └── process_netlink_messages()
│       ├── RTM_NEWLINK / RTM_DELLINK
│       ├── RTM_NEWADDR / RTM_DELADDR
│       └── RTM_NEWROUTE / RTM_DELROUTE
├── CLI fd 可读
│   └── cli_handle_connection()
└── 周期性任务
    ├── metrics_poll_once()
    └── alert_check_cycle()
```

当前采用单线程 `epoll` 模型，便于保持较低资源开销。后续如需进一步提升高频事件处理能力，可以引入有界队列和 worker 解耦接收与状态更新。

## 5. 模块职责

| 模块 | 文件 | 职责 |
|---|---|---|
| 进程入口 | `src/main.c` | 初始化、信号处理、`epoll` 主循环、周期性任务调度 |
| Netlink | `src/netlink.c` / `src/netlink.h` | 创建 `NETLINK_ROUTE` socket，订阅并解析链路、地址和路由事件 |
| 状态模型 | `src/parser.c` / `src/parser.h` | 维护接口状态表、地址列表、统计计数器和快照接口 |
| Metrics | `src/metrics.c` / `src/metrics.h` | 触发周期性接口统计刷新 |
| Alert | `src/alert.c` / `src/alert.h` | 检查错误计数和高流量并输出告警日志 |
| CLI | `src/cli.c` / `src/cli.h` | 提供 Unix Domain Socket 查询入口 |
| Logger | `src/logger.c` / `src/logger.h` | 输出带时间戳的 `INFO` / `WARN` / `ERROR` 日志 |

## 6. 状态模型

`parser` 模块是当前项目的状态中心，维护全局接口链表：

```c
iface_info_t *iface_list;
```

核心结构：

- `iface_info_t`：接口名、接口索引、UP/DOWN 状态、统计计数器、地址数组。
- `iface_addr_t`：地址族、CIDR 前缀、地址字符串。

状态来源：

1. 启动时通过 `getifaddrs()` 建立初始接口表。
2. 运行时通过 Netlink 事件更新状态。
3. 统计数据优先通过 `RTM_GETLINK` 获取 `IFLA_STATS64`，失败时回退读取 `/sys/class/net/<ifname>/statistics/*`。

状态消费者：

- `cli`：查询接口状态。
- `metrics`：刷新接口计数器。
- `alert`：遍历状态表进行告警检查。

## 7. 当前工程边界

当前项目已经具备 Agent 雏形，但还有以下工程化边界需要继续加强：

- `conf/nlagent.conf` 尚未完整接入代码。
- `systemd/nlagent.service` 的 `ExecStart` 与 `Makefile install` 路径不一致。
- 退出时的 fd、Unix Socket 文件和状态表清理还不完整。
- `RTM_DELLINK` 的接口生命周期处理可以更精确。
- 运行时 metrics 还没有统一的内部统计模型。
- Netlink 接收和状态更新还没有队列解耦。

## 8. 演进原则

后续不要追求功能堆砌，而是围绕一条主线做深：

> 把 Netlink 事件稳定、低开销、可观测地转化为用户态网络状态模型。

优先级：

1. **工程稳定性**：graceful shutdown、配置化、systemd/install 闭环、异常路径处理。
2. **IO 正确性**：non-blocking、drain loop、accept/read loop、`EAGAIN`/`EINTR`/`ENOBUFS` 处理。
3. **状态一致性**：明确 `parser` 是 SSOT，整理锁边界，CLI 使用快照。
4. **可观测性**：增加事件数、错误数、最后事件时间、CLI 连接数等 runtime metrics。
5. **高性能解耦**：引入有界 ring buffer，解耦 Netlink 接收和状态更新。
6. **验证体系**：压测、sanitizer、valgrind、CI。

## 9. 推荐阶段任务

### 阶段一：从 demo 变成工程项目

- 完善 graceful shutdown。
- 支持 `-c /path/to/nlagent.conf`。
- 对齐 `Makefile install` 与 systemd service。
- 强化 non-blocking `epoll` 细节。
- 整理状态模型锁边界。
- 增加 CLI `show metrics`。

### 阶段二：做深核心技术亮点

- 正确处理 `RTM_NEWLINK` / `RTM_DELLINK` 生命周期。
- 增强状态字段：`mtu`、`flags`、`operstate`。
- 增加 `show interface <name>` 和 JSON 输出。
- 引入有界 ring buffer 和 backpressure 指标。

### 阶段三：高级加分

- Prometheus `/metrics` exporter。
- Route table 状态维护。
- benchmark、`perf`、flamegraph。
- CI、sanitizer、valgrind。
