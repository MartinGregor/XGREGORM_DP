#!/bin/bash
# xgregorm: seria s plnym realizmom. Po preruseni staci spustit znova, dokoncene behy preskoci.
# Pouzitie: JOBS=4 ./xgregorm_logs/ser.sh  (JOBS = behov naraz, predvolene 1; beh s 280 autami ~6 GB RAM)
L="$(cd "$(dirname "$0")" && pwd)"
cd "$L/.." || exit 1
JOBS=${JOBS:-1}
R="--map city-center --sim-time 60 --warmup 30 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0 --coex 1 --dcc 1 --nr-cr 1 --phone-share 0.5 --phone-kbps 20 --lte-bw 50"
declare -A P=([A]="" [E]="--qos-rat 1" [P]="--pred-rat 1")
# hotovy = ma finalny vypis merania; samotny "Wall clock" nestaci, skript ho vypise aj po zabiti simulacie
done_() { grep -q "^XGREGORM-V2V," "$L/$1.log" 2>/dev/null && grep -q "Wall clock" "$L/$1.log"; }
run() { # meno, argumenty
  rm -rf "src/automotive/examples/xgregorm_$1" "xgregorm_tmp_$1"
  timeout 86400 python3 xgregorm_gen_scenario.py $R $2 --name "$1" > "$L/$1.log" 2>&1
  done_ "$1" && rm -rf "src/automotive/examples/xgregorm_$1"
}
start() { # meno, argumenty: pocka na volne miesto; rozostup 20 s kvoli volnemu portu SUMO
  done_ "$1" && return
  while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n; done
  run "$1" "$2" &
  sleep 20
}
(while :; do echo "$(date +%F_%T) $(free -m | awk 'NR==2{print "used",$3,"avail",$7}') sim=$(pgrep -c -f hybrid-optimized)" >> "$L/mem.log"; sleep 60; done) &
MON=$!
disown $MON
for k in A E P; do start "L$k" "--vehicles 140 ${P[$k]}"; done
for k in A E P; do start "R$k" "--vehicles 280 ${P[$k]}"; done
for k in A E P; do start "M$k" "--vehicles 280 --equip W:0.3,N:0.2,WN:0.3,NONE:0.2 ${P[$k]}"; done
wait
kill $MON
for n in LA LE LP RA RE RP MA ME MP; do done_ "$n" || { echo "nedokoncene: $n"; exit 1; }; done
echo FINISHED > "$L/serDone"
