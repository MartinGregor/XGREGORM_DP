#!/bin/bash
# xgregorm: druha ladiaca seria: zopakovanie padnutych behov zmesi vybavy, prepinanie podla pretazenia, kombinacie, predikcia.
L="$(cd "$(dirname "$0")" && pwd)"; O="$L/dev"; mkdir -p "$O"
cd "$L/.." || exit 1
export SUMO_HOME=${SUMO_HOME:-/usr/local/share/sumo}
JOBS=${JOBS:-8}
B="--map city-center --vehicles 140 --sim-time 60 --warmup 30 --run hybrid --channel urban --security 1 --spat 1 --cpm-share 1.0 --coex 1 --dcc 1 --nr-cr 1 --phone-share 0.5 --phone-kbps 20 --lte-bw 50 --wifi-bcast-rate 6 --nr-freq 5.885e9 --nr-bw 20"
EQ="--equip W:0.3,N:0.2,WN:0.3,NONE:0.2"
declare -A C=(
 [EQF]="--clean 1 --init-main 0 $EQ"
 [I2]="--clean 1 --init-main 0 --nb-equip 1 $EQ"
 [LOAD]="--clean 1 --init-main 0 --load-rat 1"
 [I14]="--clean 1 --init-main 0 --dcc-offload 50 --dup-near 60"
 [I6R]="--clean 1 --init-main 1 --cpm-rat W"
 [PM]="--clean 1 --init-main 2 --pred-rat 1 --probe-hz 0.5 --rem-period 1"
 [PL]="--clean 1 --init-main 2 --pred-rat 1 --probe-hz 0.5 --rem-period 1 --pred-model xgregorm_pred_model.txt"
)
ok() { grep -q "^XGREGORM-V2V," "$O/$1.log" 2>/dev/null && grep -q "Wall clock" "$O/$1.log"; }
run() {
  rm -rf "src/automotive/examples/xgregorm_$1" "xgregorm_tmp_$1"
  timeout 43200 python3 xgregorm_gen_scenario.py $B $2 --name "$1" > "$O/$1.log" 2>&1
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
  for c in EQF I2 LOAD I14 I6R PM PL; do start "${c}_s$s" "${C[$c]} --seed $s"; done
done
wait
kill $MON
rm -f "$O/dev2Fail"; for c in EQF I2 LOAD I14 I6R PM PL; do for s in 101 102 103; do ok "${c}_s$s" || echo "${c}_s$s" >> "$O/dev2Fail"; done; done
echo FINISHED > "$O/dev2Done"
