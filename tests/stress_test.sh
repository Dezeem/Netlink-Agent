#!/bin/bash
# Netlink Agent 压力测试
# 用法: sudo bash tests/stress_test.sh [选项]
#
# 选项:
#   -d <秒>    持续时间 (默认: 60)
#   -v <数量>  veth 每批创建数 (默认: 200, 设为0关闭)
#   -a <数量>  地址增删每轮数量 (默认: 50, 设为0关闭)
#   -c <数量>  CLI 短连接并发数 (默认: 30, 设为0关闭)
#   -l <0|1>   CLI 长连接压测 (默认: 1=开启, 0=关闭)
#   -p         跑 perf benchmark (三级强度 + perf stat 报告)
#   -h         显示帮助
#
# 示例:
#   sudo bash tests/stress_test.sh                    # 默认参数
#   sudo bash tests/stress_test.sh -d 120 -v 500      # 2 分钟 veth 风暴
#   sudo bash tests/stress_test.sh -p                 # perf 基准测试
#   sudo bash tests/stress_test.sh -v 0 -a 0 -l 0 -c 50  # 纯 CLI 压测

set -euo pipefail

AGENT_SOCK="/tmp/nlagent.sock"
DURATION=60
VETH_BATCH=200
ADDR_BATCH=50
CLI_CONCURRENT=30
CLI_LONG=1
PERF_MODE=0

while getopts "d:v:a:c:l:ph" opt; do
    case "$opt" in
        d) DURATION="$OPTARG"  ;;
        v) VETH_BATCH="$OPTARG" ;;
        a) ADDR_BATCH="$OPTARG" ;;
        c) CLI_CONCURRENT="$OPTARG" ;;
        l) CLI_LONG="$OPTARG"   ;;
        p) PERF_MODE=1          ;;
        h) sed -n '2,/^$/p' "$0" | sed 's/^# //'; exit 0 ;;
        *) exit 1 ;;
    esac
done

# perf mode: delegate to perf_bench.sh
if [ "$PERF_MODE" -eq 1 ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    exec bash "$SCRIPT_DIR/perf_bench.sh"
fi

END=$((SECONDS + DURATION))

cleanup() {
    echo ""
    echo "=== 清理中 ==="
    for n in $(ip -o link show 2>/dev/null | awk -F': ' '{print $2}' \
              | grep -E '^stress|^veth|^vpeer' || true); do
        ip link del "$n" 2>/dev/null || true
    done
    kill $(jobs -p) 2>/dev/null || true
    wait 2>/dev/null || true
    echo "=== 清理完成 ==="
}
trap cleanup EXIT

if [ "$EUID" -ne 0 ]; then echo "请用 sudo 运行" >&2; exit 1; fi

AGENT_PID=$(pgrep nlagent 2>/dev/null || true)
echo "=========================================="
echo " Netlink Agent 压测工具"
echo "=========================================="
echo " 持续时间:         ${DURATION}s"
echo " veth 每批:        ${VETH_BATCH}"
echo " 地址每轮:         ${ADDR_BATCH}"
echo " CLI 短连接并发:   ${CLI_CONCURRENT}"
echo " CLI 长连接:       $([ "$CLI_LONG" -eq 1 ] && echo "开启" || echo "关闭")"
echo " Agent PID:        ${AGENT_PID:-未运行}"
echo "=========================================="
echo ""

if [ -z "$AGENT_PID" ]; then echo "[!] agent 未启动，请先: sudo ./build/nlagent"; fi
if [ ! -S "$AGENT_SOCK" ]; then echo "[!] socket 不存在 ($AGENT_SOCK)"; fi
echo ""

# ── 任务 1: veth 创建/删除风暴 ──
if [ "$VETH_BATCH" -gt 0 ]; then
    (
        round=0
        while [ $SECONDS -lt $END ]; do
            batch=$((round * VETH_BATCH))
            for i in $(seq 1 "$VETH_BATCH"); do
                id=$((batch + i))
                ip link add "stress$id" type veth peer name "stressp$id" 2>/dev/null || true
            done
            for i in $(seq 1 "$VETH_BATCH"); do
                id=$((batch + i))
                ip link del "stress$id" 2>/dev/null || true
            done
            ((round++))
        done
    ) &
    echo "[已启动] 任务1 - veth 风暴 (${VETH_BATCH} 对/轮)"
fi

# ── 任务 2: 地址增删风暴 ──
if [ "$ADDR_BATCH" -gt 0 ]; then
    (
        IFACE=$(ip -o link show 2>/dev/null | awk -F': ' '{print $2}' | grep -v lo | head -1 || true)
        if [ -n "$IFACE" ]; then
            i=0
            while [ $SECONDS -lt $END ]; do
                for j in $(seq 1 "$ADDR_BATCH"); do
                    ip addr add "192.168.${i}.${j}/24" dev "$IFACE" 2>/dev/null || true
                done
                for j in $(seq 1 "$ADDR_BATCH"); do
                    ip addr del "192.168.${i}.${j}/24" dev "$IFACE" 2>/dev/null || true
                done
                ((i++))
            done
        fi
    ) &
    echo "[已启动] 任务2 - 地址风暴 (${ADDR_BATCH} IP/轮)"
fi

# ── 任务 3: CLI 长连接 ──
if [ "$CLI_LONG" -eq 1 ] && [ -S "$AGENT_SOCK" ]; then
    (
        while [ $SECONDS -lt $END ]; do
            printf "show interfaces\nhelp\nlist\nshow interfaces\nhelp\n" \
                | socat - UNIX-CONNECT:"$AGENT_SOCK" > /dev/null 2>&1 || true
        done
    ) &
    echo "[已启动] 任务3 - CLI 长连接"
fi

# ── 任务 4: CLI 短连接风暴 ──
if [ "$CLI_CONCURRENT" -gt 0 ] && [ -S "$AGENT_SOCK" ]; then
    (
        while [ $SECONDS -lt $END ]; do
            for i in $(seq 1 "$CLI_CONCURRENT"); do
                ( echo "show interfaces" | nc -q 0 -U "$AGENT_SOCK" > /dev/null 2>&1 || true ) &
            done
            wait
        done
    ) &
    echo "[已启动] 任务4 - CLI 短连接 (${CLI_CONCURRENT} 并发)"
fi

echo ""
echo "── 监控输出 ──────────────────────────────"
printf "%-6s %-6s %-7s %-9s %-8s %-8s\n" "时间" "FD数" "CPU%" "内存(KB)" "接口数" "线程数"
printf "%-6s %-6s %-7s %-9s %-8s %-8s\n" "────" "────" "─────" "───────" "─────" "─────"

while [ $SECONDS -lt $END ]; do
    pid=$(pgrep nlagent 2>/dev/null || true)
    if [ -n "$pid" ]; then
        fd=$(ls -1 "/proc/$pid/fd" 2>/dev/null | wc -l)
        cpu=$(ps -p "$pid" -o %cpu --no-headers 2>/dev/null | tr -d ' ' || echo "?")
        mem=$(ps -p "$pid" -o rss --no-headers 2>/dev/null | tr -d ' ' || echo "?")
        ifaces=$(ip -o link show 2>/dev/null | wc -l)
        ths=$(ps -p "$pid" -o nlwp --no-headers 2>/dev/null | tr -d ' ' || echo "?")
        printf "%-6s %-6s %-7s %-9s %-8s %-8s\n" \
            "$((SECONDS))" "$fd" "${cpu}%" "${mem}" "$ifaces" "$ths"
    else
        printf "%-6s %-6s %-7s %-9s %-8s %-8s\n" "$((SECONDS))" "-" "-" "-" "-" "-"
    fi
    sleep 2
done

echo ""
echo "=== 压测完成 ==="
