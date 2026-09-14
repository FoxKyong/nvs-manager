# Shared by tools/hw-*: finds a named hardware runner and talks to it over SSH.
# Source it; it is not a command of its own.

set -euo pipefail

HW_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
HW_CONF="${NVSM_RUNNERS_CONF:-$HW_ROOT/hardware-runners.conf}"

# NVS on both the standalone layout (partitions.csv) and Launcher's.
HW_NVS_OFFSET=0x9000
HW_NVS_SIZE=0x5000

hw_die() {
    echo "${0##*/}: $*" >&2
    exit 1
}

# hw_load NAME: sets HW_SSH, HW_PORT, HW_DIR, HW_PY, HW_ESPTOOL.
hw_load() {
    local name="${1:-}" line
    [[ -n "$name" ]] || hw_die "no runner name given (see hardware-runners.conf.example)"
    [[ -f "$HW_CONF" ]] || hw_die "$HW_CONF not found; copy hardware-runners.conf.example and fill it in"
    line="$(awk -v n="$name" '$1 == n { print; exit }' "$HW_CONF")"
    [[ -n "$line" ]] || hw_die "runner '$name' is not in $HW_CONF"
    read -r _ HW_SSH HW_PORT HW_DIR _ <<<"$line"
    [[ -n "${HW_DIR:-}" ]] || hw_die "runner '$name' needs four fields: name ssh-target serial-port runner-dir"
    HW_PY="$HW_DIR/venv/bin/python"
    HW_ESPTOOL="$HW_DIR/venv/bin/esptool"
    HW_PORT_Q="$(printf '%q' "$HW_PORT")"
}

# hw_ssh COMMAND-LINE: runs it on the runner (from the SSH user's home).
hw_ssh() {
    ssh -o BatchMode=yes "$HW_SSH" "$@"
}

# Like hw_ssh, with a terminal when there is one, so Ctrl-C reaches the runner.
hw_ssh_interactive() {
    if [[ -t 0 ]]; then
        ssh -tt -o BatchMode=yes "$HW_SSH" "$@"
    else
        hw_ssh "$@"
    fi
}

# Copies the runner-side helpers (tools/runner/*.py) to <runner-dir>/tools.
hw_sync() {
    hw_ssh "mkdir -p $HW_DIR/tools"
    scp -q "$HW_ROOT"/tools/runner/*.py "$HW_SSH:$HW_DIR/tools/"
}

# hw_quote ARGS...: one shell-quoted string for a remote command line.
hw_quote() {
    local out="" a
    for a in "$@"; do out+="$(printf '%q ' "$a")"; done
    printf '%s' "$out"
}
