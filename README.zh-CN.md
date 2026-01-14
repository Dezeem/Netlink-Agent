# Netlink-Agent
Netlink-Agent 是一个基于 Unix Socket 的网络接口状态监控和管理工具。它能够实时监控网络接口的状态变化，并通过命令行接口提供查询功能。
## 功能特点
- 监控网络接口的状态变化（如UP/DOWN状态）。
- 支持通过 Unix Socket 进行命令行交互。
- 提供接口状态查询功能。
## 安装
1. 克隆仓库：
    ```bash
    git clone git@github.com:Dezeem/Netlink-Agent.git
    ```
2. 进入项目目录：
    ```bash
    cd Netlink-Agent
    ```
3. 构建项目
    ```bash
    make
    ```
## 使用
1. 启动 Netlink-Agent：
    ```bash
    make run
    ```
2. 使用命令行工具连接到 Unix Socket：
    ```bash
    nc -U /tmp/nlagent.sock
    ```
3. 可用命令：
    - `show interfaces` 或 `list`：显示所有接口的状态和配置信息。
## 示例
```bash
$ nc -U /tmp/nlagent.sock
> show interfaces
=== Network Interfaces (2) ===
Interface: eth0
  Index: 2, Status: UP
  Counters: RX=3534918288 TX=2293304849 RX_ERR=0 TX_ERR=0
  Addresses (3):
    [1] 10.4.4.10 (IPv4)
    [2] 192.168.88.123 (IPv4)

Interface: lo
  Index: 1, Status: UP
  Counters: RX=147969624 TX=147969624 RX_ERR=0 TX_ERR=0
  Addresses (2):
    [1] 127.0.0.1 (IPv4)
    [2] ::1 (IPv6)
```
## 测试接口添加和删除IP地址
以 eth0 为例：
- 添加 IP 地址：
    ```bash
    ip addr add 192.168.1.1 dev eth0
    ```
- 删除 IP 地址：
    ```bash
    ip addr del 192.168.1.1 dev eth0
    ```
- 查看接口状态：
    ```bash
    ip addr show dev eth0
    ```
## 贡献
欢迎提交 issue 和 pull request 来改进 NLAgent。
## 许可证
本项目采用 MIT 许可证，详情请参阅 LICENSE 文件。
