#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
output="${1:-${root}/out/web-platform-probe}"
mkdir -p "${output}"

emcc "${root}/Engine/libs/SQLite/sqlite3.c" \
  -I"${root}/Engine/libs/SQLite" -O2 -pthread \
  -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_MMAP -DSQLITE_OMIT_WAL \
  -c -o "${output}/sqlite3.o"

em++ \
  "${root}/Engine/modern/host/web/probes/web_platform_probe.cpp" \
  "${output}/sqlite3.o" \
  -I"${root}/Engine/libs/SQLite" \
  -std=c++17 -O2 -pthread \
  -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_OMIT_MMAP -DSQLITE_OMIT_WAL \
  -sWASMFS -sFORCE_FILESYSTEM -sPROXY_TO_PTHREAD \
  -sPTHREAD_POOL_SIZE=2 -sALLOW_MEMORY_GROWTH=1 -sMAXIMUM_MEMORY=1GB \
  -sEXIT_RUNTIME=1 -sASSERTIONS=1 \
  -o "${output}/index.html"

echo "${output}/index.html"
