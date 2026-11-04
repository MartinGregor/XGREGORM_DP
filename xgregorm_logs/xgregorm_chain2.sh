#!/bin/bash
# xgregorm: po final1 postupne spusti final2 a overenie napadov 1-5, aby vypocet nestal, ak Claude prave nebezi.
# Kazda seria je pokracovatelna; nezdareny beh zapise <seria>Fail a retazec ide dalej.
L="$(cd "$(dirname "$0")" && pwd)"
prev=final1
for s in final2 idea1 idea2 idea3 idea4 idea5; do
  until [ -f "$L/$prev/${prev}Done" ]; do sleep 60; done
  [ -f "$L/$s/${s}Done" ] || "$L/xgregorm_start.sh" "$L/$s.json"
  sleep 30
  prev=$s
done
