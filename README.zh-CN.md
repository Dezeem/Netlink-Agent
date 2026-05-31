# Netlink-Agent

`Netlink-Agent` 是一个基于 `Netlink + epoll` 的 Linux 网络状态监控与事件采集 Agent。它订阅内核 `NETLINK_ROUTE` 事件，将链路、地址和路由变化转化为用户态网络状态模型，并通过 Unix Domain Socket CLI 提供查询能力。

项目目标不是做一个“能监听 Netlink 的 demo”，而是逐步演进为一个具备基础设施味道的 Linux 网络监控 Agent：事件驱动、低开销、可观测、可部署、可扩展。

## 核心能力

- **事件驱动**：基于 `epoll` 统一处理 Netlink socket 与 CLI socket。
- **内核事件监听**：订阅 link、IPv4/IPv6 address、IPv4/IPv6 route 事件。
- **用户态状态模型**：维护接口状态表，记录接口索引、UP/DOWN 状态、统计计数器与地址列表。
- **低开销查询接口**：通过 Unix Domain Socket 暴露 CLI，无需引入额外 HTTP 依赖。
- **指标刷新与告警**：周期性刷新接口 RX/TX 统计，并对错误计数和高流量进行基础告警。
- **工程化基础**：提供 `Makefile`、示例配置和 systemd 服务模板。

## 架构概览

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

更多架构说明见 `ARCHITECTURE.md`。

## 目录结构

```text
Netlink-Agent/
├── conf/
│   └── nlagent.conf          # 示例配置，当前配置化能力待完善
├── src/
│   ├── main.c                # 入口、初始化、epoll 主循环
│   ├── netlink.c/.h          # Netlink socket 与事件解析
│   ├── parser.c/.h           # 用户态接口状态模型
│   ├── metrics.c/.h          # 周期性指标刷新入口
│   ├── alert.c/.h            # 基础告警检查
│   ├── cli.c/.h              # Unix Socket CLI
│   └── logger.c/.h           # 日志输出
├── systemd/
│   └── nlagent.service       # systemd 服务模板
├── Makefile
├── README.md
├── README.zh-CN.md
└── LICENSE
```

## 快速开始

### 构建

```bash
make
```

构建产物：

```text
build/nlagent
```

### 运行

`Netlink-Agent` 需要访问内核网络状态，建议使用 root 权限运行：

```bash
sudo ./build/nlagent
```

或：

```bash
make run
```

### 使用 CLI 查询

```bash
nc -U /tmp/nlagent.sock
```

可用命令：

| 命令 | 说明 |
|---|---|
| `show interfaces` | 显示所有接口状态、统计计数器和地址列表 |
| `list` | `show interfaces` 的别名 |
| `help` | 显示帮助 |
| `quit` / `exit` | 关闭当前连接 |

示例：

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

## 当前技术亮点

### 1. Netlink 事件驱动

项目直接监听内核 `NETLINK_ROUTE`，相比周期性执行 `ip addr` 或读取命令输出，具备更低延迟和更低开销。

### 2. epoll 单线程事件循环

主循环统一处理 Netlink 与 CLI 事件，避免为每个连接创建线程，适合作为轻量级系统 Agent 的基础模型。

### 3. 用户态状态模型

`parser` 模块维护接口状态表，CLI、metrics 和 alert 都围绕这份状态模型工作，避免各模块重复采集系统状态。

### 4. 快照式 CLI 查询

CLI 查询通过状态表快照输出接口信息，减少查询逻辑与底层链表状态的耦合。

## 当前边界

以下能力已经有基础文件或雏形，但仍需继续工程化：

- `conf/nlagent.conf` 当前是示例配置，代码尚未完整加载。
- `systemd/nlagent.service` 与 `make install` 的安装路径需要对齐。
- Netlink、CLI、状态表的异常路径和资源清理还可以继续加强。
- 运行时 metrics、JSON 输出、Prometheus exporter、消息队列解耦属于后续演进方向。

## 路线图

优先围绕“高性能 + 可观测 + 工程稳定性”做深：

1. 完善 graceful shutdown，关闭 fd、删除 socket、释放状态表。
2. 实现配置化，支持 `-c /path/to/nlagent.conf`。
3. 对齐 `Makefile install` 与 `systemd` 部署路径。
4. 强化 non-blocking + epoll 细节，完善 drain、accept/read 循环和异常处理。
5. 整理状态模型锁边界，明确 `parser` 作为 SSOT。
6. 正确处理接口生命周期，区分 `RTM_NEWLINK` 与 `RTM_DELLINK`。
7. 增加运行时 metrics，并通过 CLI 输出 `show metrics`。
8. 增强 CLI：`show interface <name>`、JSON 输出。
9. 引入有界 ring buffer 解耦 Netlink 接收与状态更新。
10. 补充压测、sanitizer、valgrind 和 CI。

## 许可证

本项目采用 MIT 许可证，详情见 `LICENSE`。
