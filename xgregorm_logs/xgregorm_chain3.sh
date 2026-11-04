#!/bin/bash
# xgregorm: po idea5 spusti idea6 (I6R). Samostatne, lebo xgregorm_chain2.sh uz bezi a bezaci skript sa nema menit.
L="$(cd "$(dirname "$0")" && pwd)"
until [ -f "$L/idea5/idea5Done" ]; do sleep 60; done
sleep 30
[ -f "$L/idea6/idea6Done" ] || "$L/xgregorm_start.sh" "$L/idea6.json"
