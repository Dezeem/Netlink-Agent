# NLAgent
NLAgent 是一个基于 Unix Socket 的网络接口状态监控和管理工具。它能够实时监控网络接口的状态变化，并通过命令行接口提供查询功能。
## 功能特点
- 监控网络接口的状态变化（如UP/DOWN状态）。
- 支持通过 Unix Socket 进行命令行交互。
- 提供接口状态查询功能。
## 安装
1. 克隆仓库：
    ```bash
    git clone
    ```
2. 进入项目目录：
    ```bash
    cd nlagent
    ```
3. 安装依赖：
    ```bash
    go mod tidy
    ```
4. 编译项目：
    ```bash
    go build -o nlagent main.go
    ```
## 使用
1. 启动 NLAgent：
    ```bash
    ./nlagent
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
Interface: eth0
  Status: UP
  IPs:
    - 192.168.1.1
Interface: wlan0
  Status: DOWN
  IPs:
    - None
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
