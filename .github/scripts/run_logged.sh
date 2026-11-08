#!/usr/bin/env bash
# Runs a command and, if it fails, publishes the interesting lines as GitHub annotations so the
# failure is readable from the run summary (and the API) without downloading logs.
#   usage: run_logged.sh <title> <command...>
title="$1"
shift
log="$(mktemp)"
set +e
"$@" 2>&1 | tee "$log"
status=${PIPESTATUS[0]}
if [ "$status" -ne 0 ]; then
  {
    grep -E "error:|Error |FAILED|undefined reference|fatal error|\[FAIL\]|  FAIL|cannot |not found|No such file|CMake Error" "$log" | head -n 6
    echo "---- last lines ----"
    tail -n 4 "$log"
  } | head -n 10 | while IFS= read -r line; do
    line="${line//$'\r'/}"
    line="${line//%/%25}"
    echo "::error title=${title} failed (exit ${status})::${line:0:900}"
  done
fi
exit "$status"
