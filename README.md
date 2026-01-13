# NLAgent
NLAgent is a Unix-socket-based network interface state monitoring and management tool. It monitors network interface state changes in real time and provides query capabilities via a command-line interface.

## Features
- Monitor network interface state changes (e.g., UP/DOWN).
- Support command-line interaction via Unix Socket.
- Provide interface status and configuration queries.

## Installation
1. Clone the repository (replace <your-repo-url> with the actual repository URL):
    ```bash
    git clone https://github.com/yourusername/nlagent.git
    ```
2. Change to the project directory:
    ```bash
    cd nlagent
    ```
3. Install dependencies:
    ```bash
    go mod tidy
    ```
4. Build the project:
    ```bash
    go build -o nlagent main.go
    ```

## Usage
1. Start NLAgent:
    ```bash
    ./nlagent
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
Interface: eth0
  Status: UP
  IPs:
    - 192.168.1.1
Interface: wlan0
  Status: DOWN
  IPs:
    - None
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
