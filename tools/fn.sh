#!/usr/bin/env bash
# Print the decompile of one function from re/all.c: tools/fn.sh 00404630
awk -v a="@ $1" 'index($0,"// ==== ")==1 { p = index($0,a) > 0 } p' "$(dirname "$0")/../re/all.c"
