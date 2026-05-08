#!/usr/bin/env bash
# File: scripts/stop_hospital.sh

set -u

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LOG_DIR="$PROJECT_ROOT/logs"
PID_FILE="$LOG_DIR/hospital.pid"
FIFO_PATH="/tmp/hospital_triage_fifo"
SHM_KEY="0x48807123"

say() {
    printf '[%s] STOP: %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$1"
}

pid_is_running() {
    local check_pid="$1"

    if [[ -z "$check_pid" ]]; then
        return 1
    fi

    kill -0 "$check_pid" 2>/dev/null
}

cd "$PROJECT_ROOT" || exit 1

if [[ ! -f "$PID_FILE" ]]; then
    say "no PID file found"
    say "hospital simulator is probably not running"
else
    hospital_pid="$(cat "$PID_FILE")"

    if ! [[ "$hospital_pid" =~ ^[0-9]+$ ]]; then
        say "PID file contains invalid data"
        rm -f "$PID_FILE"
    elif ! pid_is_running "$hospital_pid"; then
        say "PID $hospital_pid is not running"
        rm -f "$PID_FILE"
        say "removed stale PID file"
    else
        say "sending SIGTERM to PID $hospital_pid"
        kill "$hospital_pid"

        for second in 1 2 3 4 5; do
            sleep 1

            if ! pid_is_running "$hospital_pid"; then
                rm -f "$PID_FILE"
                say "hospital simulator stopped cleanly"
                break
            fi

            say "waiting for shutdown... ${second}s"
        done

        if pid_is_running "$hospital_pid"; then
            say "process did not stop after SIGTERM"
            say "sending SIGKILL to PID $hospital_pid"
            kill -9 "$hospital_pid"
            rm -f "$PID_FILE"
            say "forced shutdown completed"
        fi
    fi
fi

rm -f "$FIFO_PATH"
ipcrm -M "$SHM_KEY" 2>/dev/null || true
say "IPC cleanup attempted"
