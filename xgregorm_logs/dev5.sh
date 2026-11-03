#!/bin/bash
# xgregorm: piata ladiaca seria: horna hranica prinosu predikcie. Referencie O* poznaju zony presne (nie su realizovatelne).
# O3 - O0 = najvacsia hodnota pozerania dopredu, O0 - Q = hodnota okamzitej znalosti stavu. PZ = model naučeny na zonach,
# vzdy na dvoch inych seedoch. R50 = rovnake zony s polomerom 50 m (kratky prejazd, reakcia ma mensi naskok).
L="$(cd "$(dirname "$0")" && pwd)"; O="$L/dev5"; mkdir -p "$O"
cd "$L/.." || exit 1
export SUMO_HOME=${SUMO_HOME:-/usr/local/share/sumo}
JOBS=${JOBS:-8}
zn() { echo "0:$1:0:999:W:0.7+2:$1:0:999:N:0.7+5:$1:0:999:W:0.7+8:$1:0:999:N:0.7"; }
B="--map city-center --vehicles 140 --sim-time 120 --warmup 30 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0 --coex 1 --dcc 1 --nr-cr 1 --phone-share 0.5 --phone-kbps 20 --lte-bw 50 --wifi-bcast-rate 6 --nr-freq 5.885e9 --nr-bw 20 --wifi-rx-min -88"
ORA="--clean 1 --init-main 2 --qos-rat 1 --oracle 1"
declare -A C=(
 [O0]="--zones $(zn 150) $ORA --pred-horizon 0"
 [O3]="--zones $(zn 150) $ORA --pred-horizon 3"
 [O8]="--zones $(zn 150) $ORA --pred-horizon 8"
 [PZ]="--zones $(zn 150) --clean 1 --init-main 2 --pred-rat 1 --probe-hz 0.5 --rem-period 1"
 [R50MIX]="--zones $(zn 50) --clean 1 --init-main 2"
 [R50Q]="--zones $(zn 50) --clean 1 --init-main 2 --qos-rat 1 --probe-hz 0.5"
 [R50PM]="--zones $(zn 50) --clean 1 --init-main 2 --pred-rat 1 --probe-hz 0.5 --rem-period 1"
 [R50O0]="--zones $(zn 50) $ORA --pred-horizon 0"
 [R50O3]="--zones $(zn 50) $ORA --pred-horizon 3"
)
ALL="O0 O3 O8 PZ R50MIX R50Q R50PM R50O0 R50O3"
ok() { grep -q "^XGREGORM-V2V," "$O/$1.log" 2>/dev/null && grep -q "Wall clock" "$O/$1.log"; }
run() {
  rm -rf "src/automotive/examples/xgregorm_$1" "xgregorm_tmp_$1"
  timeout 86400 python3 xgregorm_gen_scenario.py $B $2 --name "$1" > "$O/$1.log" 2>&1
  ok "$1" && rm -rf "src/automotive/examples/xgregorm_$1"
}
start() {
  ok "$1" && return
  while [ "$(jobs -r | wc -l)" -ge "$JOBS" ]; do wait -n; done
  run "$1" "$2" &
  sleep 20
}
(while :; do echo "$(date +%F_%T) $(free -m | awk 'NR==2{print "used",$3,"avail",$7}') sim=$(pgrep -c -f hybrid-optimized)" >> "$O/mem.log"; sleep 60; done) &
MON=$!; disown $MON
for s in 101 102 103; do
  for c in $ALL; do
    x="${C[$c]} --seed $s"; [ "$c" = PZ ] && x="$x --pred-model $O/model_bez_s$s.txt"
    start "${c}_s$s" "$x"
  done
done
wait
kill $MON
rm -f "$O/dev5Fail"; for c in $ALL; do for s in 101 102 103; do ok "${c}_s$s" || echo "${c}_s$s" >> "$O/dev5Fail"; done; done
echo FINISHED > "$O/dev5Done"
