#!/bin/sh
# Scripting interface: exit statuses, -q -p -x, script arguments, `.`, `exit`, stdin.
#   tests/run_scripts.sh BIN
BIN=${1:-./bin/release/mm41}
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
export XDG_CONFIG_HOME="$TMP/cfg" HOME="$TMP"
pass=0; fail=0
check() {  # name expected_status expected_stdout command...
  name=$1; want_st=$2; want_out=$3; shift 3
  out=$("$@" 2>"$TMP/err"); st=$?
  if [ "$st" = "$want_st" ] && [ "$out" = "$want_out" ]; then pass=$((pass+1)); echo "PASS $name"
  else fail=$((fail+1)); echo "FAIL $name: status $st (wanted $want_st), stdout [$out] (wanted [$want_out]), stderr: $(cat "$TMP/err" | head -2)"; fi
}
cat > "$TMP/loan.mm41" <<'S'
#!/usr/bin/env mm41
# amount rate years -- monthly payment
clrtvm 12 * n 12 / i% pv 0 fv ?pmt chs .
S
cat > "$TMP/fail.mm41" <<'S'
1 0 /
"after" .
S
check "script args"        0 "1798.651575" "$BIN" -n "$TMP/loan.mm41" 300000 6 30
check "script -p"          0 "5.000000"    "$BIN" -n -p -e "2 3 +"
check "matrix -p"          0 "2.000000 4.000000
6.000000 8.000000" "$BIN" -n -p -e "[1 2; 3 4] 2 *"
check "dot prints plain"   0 "hello
0.333333" "$BIN" -n -q -e '"hello" . 1 3 / .'
check "error status"       1 "after"       "$BIN" -n "$TMP/fail.mm41"
check "-x stops"           1 ""            "$BIN" -n -x "$TMP/fail.mm41"
check "exit status"        7 ""            "$BIN" -n -q -e "7 exit" -e '"no" .'
check "stdin dash"         0 "6.000000"    sh -c "echo '2 3 * .' | '$BIN' -n -q -"
check "missing file"       2 ""            "$BIN" -n -q "$TMP/nothere.mm41"
check "usage error"        2 ""            "$BIN" -n --bogus
check "env"                0 "$TMP"        "$BIN" -n -q -e '"HOME" env .'
check "env unset"          1 ""            "$BIN" -n -q -e '"MM41_NO_SUCH_VAR" env'
check "comment lines"      0 "4.000000"    sh -c "printf '# c\n  # c\n2 2 + .\n' | '$BIN' -n -q -"
check "double dash"        0 "0.028643"    "$BIN" -n -p -- "$TMP/loan.mm41" 1 2 3
echo "scripts: $pass passed, $fail failed"
[ "$fail" = 0 ]
