#!/bin/bash
# xgregorm: po dobehnuti dev7 spusti referencne behy finalnej serie, aj keby Claude prave nebezal (napr. limit)
L="$(cd "$(dirname "$0")" && pwd)"
until [ -f "$L/dev7/dev7Done" ]; do sleep 60; done
[ -d "$L/final1" ] && grep -q . "$L/final1.out" 2>/dev/null && exit 0
"$L/xgregorm_start.sh" "$L/final1.json"
