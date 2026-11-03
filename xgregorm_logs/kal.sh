#!/bin/bash
# xgregorm: kalibracia pred seriou o predikcii: vplyv rychlosti broadcastu 802.11p (6 Mbit/s, EN 302 663 tab. 2)
# a planu pasma (NR 20 MHz vedla 802.11p). Porovnanie so scenarom L (LA, LE, LP; 3 Mbit/s, NR 40 MHz, subezny kanal).
# Po preruseni staci spustit znova, dokoncene behy preskoci. Pouzitie: JOBS=6 ./xgregorm_logs/kal.sh
L="$(cd "$(dirname "$0")" && pwd)"
cd "$L/.." || exit 1
export SUMO_HOME=${SUMO_HOME:-/usr/local/share/sumo}
JOBS=${JOBS:-6}
R="--map city-center --sim-time 60 --warmup 30 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0 --coex 1 --dcc 1 --nr-cr 1 --phone-share 0.5 --phone-kbps 20 --lte-bw 50 --vehicles 140 --wifi-bcast-rate 6"
EU="--nr-freq 5.885e9 --nr-bw 20"
declare -A P=([A]="" [E]="--qos-rat 1" [P]="--pred-rat 1")
done_() { grep -q "^XGREGORM-V2V," "$L/$1.log" 2>/dev/null && grep -q "Wall clock" "$L/$1.log"; }
run() {
  rm -rf "src/automotive/examples/xgregorm_$1" "xgregorm_tmp_$1"
  timeout 86400 python3 xgregorm_gen_scenario.py $R $2 --name "$1" > "$L/$1.log" 2>&1
  done_ "$1" && rm -rf "src/automotive/examples/xgregorm_$1"
}
start() {
  done_ "$1" && return
  while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n; done
  run "$1" "$2" &
  sleep 20
}
(while :; do echo "$(date +%F_%T) $(free -m | awk 'NR==2{print "used",$3,"avail",$7}') sim=$(pgrep -c -f hybrid-optimized)" >> "$L/mem_kal.log"; sleep 60; done) &
MON=$!
disown $MON
for k in A E P; do start "C6$k" "${P[$k]}"; done
for k in A E P; do start "C6U$k" "$EU ${P[$k]}"; done
wait
kill $MON
for n in C6A C6E C6P C6UA C6UE C6UP; do done_ "$n" || { echo "nedokoncene: $n"; exit 1; }; done
echo FINISHED > "$L/kalDone"
