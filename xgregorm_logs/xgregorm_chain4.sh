#!/bin/bash
# xgregorm: po idea1 spusti idea2 az idea6 ako jednu seriu (idea26.json), aby na konci malych serii nestali jadra.
L="$(cd "$(dirname "$0")" && pwd)"
until [ -f "$L/idea1/idea1Done" ]; do sleep 30; done
sleep 10
[ -f "$L/idea26/idea26Done" ] || "$L/xgregorm_start.sh" "$L/idea26.json"
