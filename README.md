# Netlink-Agent
Netlink-Agent is a Unix-socket-based network interface state monitoring and management tool. It monitors network interface state changes in real time and provides query capabilities via a command-line interface.

## Features
- Monitor network interface state changes (e.g., UP/DOWN).
- Support command-line interaction via Unix Socket.
- Provide interface status and configuration queries.

## Installation
1. Clone the repository:
    ```bash
    git clone git@github.com:Dezeem/Netlink-Agent.git
    ```
2. Change to the project directory:
    ```bash
    cd Netlink-Agent
    ```
3. Build the project:
    ```bash
    make
    ```
    
## Usage
1. Start Netlink-Agent:
    ```bash
    make run
    ```
2. Connect to the Unix Socket using a command-line tool:
    ```bash
    nc -U /tmp/nlagent.sock
    ```
3. Available commands:
    - `show interfaces` or `list`: Display the status and configuration of all interfaces.

## Example
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

## Testing adding and removing IP addresses
Using eth0 as an example:
- Add an IP address:
    ```bash
    ip addr add 192.168.1.1 dev eth0
    ```
- Remove an IP address:
    ```bash
    ip addr del 192.168.1.1 dev eth0
    ```
- View interface status:
    ```bash
    ip addr show dev eth0
    ```

## Contributing
Contributions are welcome. Please open issues and pull requests to improve NLAgent.

## License
This project is licensed under the MIT License. See the LICENSE file for details.
