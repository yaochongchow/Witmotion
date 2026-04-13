#!/bin/bash
#
# read_usb.sh
# ----------
# Host-side RTT capture helper for the nRF52 Witmotion relay firmware.
#
# Responsibilities:
# 1) Start a J-Link session for the target.
# 2) Attach RTT client and normalize console output.
# 3) Parse sensor lines, prepend wall-clock timestamps, and print to terminal.
# 4) Mirror parsed samples to CSV for offline analysis.
#
# Runtime knobs:
# - CSV_FILE:           output CSV file path/name
# - AUTO_RESET=0|1:     reset target before attach (default: 0 for fast attach)
# - STARTUP_DELAY_SEC:  wait after JLinkExe launch before RTT read

CSV_FILE="${CSV_FILE:-witmotion_rtt_$(date '+%Y%m%d_%H%M%S').csv}"
AUTO_RESET="${AUTO_RESET:-0}"
STARTUP_DELAY_SEC="${STARTUP_DELAY_SEC:-0.25}"

echo "Connecting to NRF52 via RTT..."
echo "Press Ctrl+C to stop"
echo "Display mode: every sample (no averaging)"
echo "Auto reset target: ${AUTO_RESET}"
echo "Startup delay: ${STARTUP_DELAY_SEC}s"
echo "CSV log: ${CSV_FILE}"
echo ""

# Write J-Link command sequence. Reset is optional for fast startup workflows.
JLINK_CMD=$(mktemp)
if [ "$AUTO_RESET" = "1" ]; then
    printf "r\ng\nsleep 999999\nexit\n" > "$JLINK_CMD"
else
    printf "g\nsleep 999999\nexit\n" > "$JLINK_CMD"
fi

# Start JLinkExe in background and keep process ID for cleanup.
JLinkExe -Device NRF52832_XXAA -If SWD -Speed 4000 -AutoConnect 1 \
    -CommandFile "$JLINK_CMD" > /dev/null 2>&1 &
JLINK_PID=$!

cleanup() { kill $JLINK_PID 2>/dev/null; rm -f "$JLINK_CMD"; exit 0; }
trap cleanup SIGINT SIGTERM

sleep "$STARTUP_DELAY_SEC"

# Read RTT stream, strip ANSI escapes, parse/print/log data rows.
JLinkRTTClient 2>/dev/null \
    | sed -u 's/\x1b\[[0-9;]*m//g' \
    | awk -v csv_file="$CSV_FILE" '
function now_ms_str(    cmd, out) {
    cmd = "date \"+%Y-%m-%d %H:%M:%S.%3N\""
    cmd | getline out
    close(cmd)
    return out
}

BEGIN {
    FS = " *\\| *"
    # Human-readable terminal header.
    print "DateTime                | Time(ms) | Seq  | Acc(x,y,z) | Gyro(x,y,z) | Q(w,x,y,z) | RSSI | Hz"
    print "------------------------+----------+------+------------+-------------+-------------+------+---"
    # CSV header so each run is self-describing.
    print "datetime,fw_time_ms,seq,acc_x,acc_y,acc_z,gyro_x,gyro_y,gyro_z,qw,qx,qy,qz,rssi_dbm,hz" > csv_file
    fflush(csv_file)
}

/\[INF\] witmotion_relay:/ {
    print
    next
}

/^[[:space:]]*[0-9]+[[:space:]]*\|[[:space:]]*Seq:/ {
    # Parse firmware line format:
    # <time_ms> | Seq:<n> | Acc(a,b,c) | Gyro(x,y,z) | Q(w,x,y,z) | RSSI:<dBm> | Hz:<n>
    fw_time = $1 + 0

    seq = $2
    sub(/^Seq:/, "", seq)
    seq += 0

    acc = $3
    sub(/^Acc\(/, "", acc)
    sub(/\)$/, "", acc)
    split(acc, a, ",")

    gyro = $4
    sub(/^Gyro\(/, "", gyro)
    sub(/\)$/, "", gyro)
    split(gyro, g, ",")

    quat = $5
    sub(/^Q\(/, "", quat)
    sub(/\)$/, "", quat)
    split(quat, q, ",")

    rssi = $6
    sub(/^RSSI:/, "", rssi)
    rssi += 0

    hz = $7
    sub(/^Hz:/, "", hz)
    hz += 0

    # Attach host timestamp (with ms) to preserve absolute wall-clock time.
    dt = now_ms_str()
    printf "%s | %u | Seq:%u | Acc(%d,%d,%d) | Gyro(%d,%d,%d) | Q(%d,%d,%d,%d) | RSSI:%d | Hz:%u\n", \
           dt, fw_time, seq, \
           a[1] + 0, a[2] + 0, a[3] + 0, \
           g[1] + 0, g[2] + 0, g[3] + 0, \
           q[1] + 0, q[2] + 0, q[3] + 0, q[4] + 0, \
           rssi, hz

    printf "%s,%u,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%u\n", \
           dt, fw_time, seq, \
           a[1] + 0, a[2] + 0, a[3] + 0, \
           g[1] + 0, g[2] + 0, g[3] + 0, \
           q[1] + 0, q[2] + 0, q[3] + 0, q[4] + 0, \
           rssi, hz >> csv_file
    fflush(csv_file)
    next
}

# Fallback: if a data-like line did not match strict parser, pass it through
# with timestamp to aid troubleshooting without silently dropping information.
/Seq:.*Acc\(.*Gyro\(.*Q\(.*RSSI:.*Hz:/ {
    if ($0 !~ /^[[:space:]]*[0-9]+[[:space:]]*\|[[:space:]]*Seq:/) {
        print now_ms_str() " | RAW | " $0
    }
}
'

cleanup
