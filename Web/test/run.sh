#!/bin/sh
# Web/test: the double-double orbit against mpmath, the orbit layout and dd functions, and the whole reference orbit
# (series rows included) against the C++ FractalMath code it was ported from.
set -e
cd "$(dirname "$0")"
python3 gen_cases.py /tmp/web_cases.json
node lib.test.js /tmp/web_cases.json
g++ -std=gnu++20 -O2 -ffp-contract=off -I../../Plugins/FractalRenderer/Source/FractalRenderer/Public orbit_dump.cpp -o /tmp/web_orbit_dump
node orbit_compare.js /tmp/web_orbit_dump
