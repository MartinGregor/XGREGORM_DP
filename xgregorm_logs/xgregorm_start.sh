#!/bin/bash
# xgregorm: spusti seriu v cgroup: simulacie spolu najviac 17 GB RAM (zvysok do swapu), aby ostala pamat pre system a Claude.
# Pouzitie: xgregorm_logs/xgregorm_start.sh spec.json
L="$(cd "$(dirname "$0")" && pwd)"
setsid nohup systemd-run --user --scope -p MemoryHigh=${MEMHIGH:-17G} -p MemorySwapMax=8G python3 "$L/xgregorm_series.py" "$1" > "${1%.json}.out" 2>&1 < /dev/null &
