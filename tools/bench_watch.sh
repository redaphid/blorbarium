#!/usr/bin/env bash
# tools/bench_watch.sh <worktree>: for every badge plugged in from now on, rebuild the worktree's
# badge128_flip (incremental: picks up whatever was merged into main since), write the merged
# .hex (keeps the pet), boot it with the log open and print one EVENT line. Badges already
# attached when it starts are left alone until unplugged (FLASH_ATTACHED=1 takes them too).
# Logs in <worktree>/.jobs/. Run it in a checkout of main so each badge gets the latest.
set -u
wt=$1
image=$wt/.pio/build/badge128_flip/merged.hex
scratch=$wt/.jobs; mkdir -p "$scratch"
et=$HOME/.local/bin/esptool.py; [ -x "$et" ] || et=$(command -v esptool.py || command -v esptool)
serial_of() { local d; d=$(readlink -f "/sys/class/tty/${1##*/}/device") || return; while [ "${#d}" -gt 1 ] && [ ! -f "$d/idVendor" ]; do d=${d%/*}; done
  [ "$(cat "$d/idVendor" 2>/dev/null):$(cat "$d/idProduct" 2>/dev/null)" = "1a86:55d3" ] && cat "$d/serial"; }
declare -A busy=()   # port -> serial, for badges already handled (or present at start)
[ -n "${FLASH_ATTACHED:-}" ] || for p in /dev/ttyACM*; do [ -e "$p" ] && s=$(serial_of "$p") && [ -n "$s" ] && busy[$p]=$s; done
echo "EVENT watching (already attached, left alone: ${busy[*]:-none})"
while true; do
  for p in "${!busy[@]}"; do [ -e "$p" ] || { echo "EVENT unplugged ${busy[$p]}"; unset "busy[$p]"; }; done
  port=""
  for p in /dev/ttyACM*; do
    [ -e "$p" ] && [ -z "${busy[$p]:-}" ] || continue
    s=$(serial_of "$p"); [ -n "$s" ] && { port=$p; ser=$s; break; }
  done
  [ -n "$port" ] || { sleep 0.5; continue; }
  busy[$port]=$ser
  echo "EVENT new badge $ser on $port: building $(git -C "$wt" log --oneline -1 | cut -c1-60)"
  if ! ~/.platformio/penv/bin/pio run -d "$wt" -e badge128_flip -j 3 > "$scratch/bench_build.log" 2>&1; then
    echo "EVENT BUILD FAILED (see $scratch/bench_build.log); $ser not flashed"; continue
  fi
  sleep 1; ok=0
  for try in 1 2 3 4; do
    [ -e "$port" ] || break
    "$et" --chip esp32s3 --port "$port" -b 460800 write_flash 0x0 "$image" > "$scratch/bench_esptool.log" 2>&1 && { ok=1; break; }
    sleep 1
  done
  if [ $ok = 0 ]; then echo "EVENT FLASH FAILED $ser: $(grep -iE 'error' "$scratch/bench_esptool.log" | tail -1)"; continue; fi
  boot=$(~/.platformio/penv/bin/python - "$port" <<'EOF'
import serial,sys,time
s=serial.Serial(); s.port,s.baudrate,s.timeout=sys.argv[1],115200,0.2; s.dtr=s.rts=False; s.open()
s.rts=True; time.sleep(0.1); s.rts=False
buf=b""; t=time.time()
while time.time()-t<25:
    buf+=s.read(4096)
    if b"ble name=" in buf: time.sleep(1); buf+=s.read(4096); break
keep=[l.split("[hw] ",1)[1] for l in buf.decode('utf-8','replace').splitlines()
      if "[hw]" in l and any(k in l for k in ("blorbarium","home","boot=","ble name","SEALED"))]
print(" | ".join(keep) or "NO BOOT LOG")
EOF
)
  echo "EVENT FLASHED $ser: $boot"
done
