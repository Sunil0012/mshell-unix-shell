#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cd "$TMP"
printf 'alpha\nbeta\n' > a.txt
printf 'gamma\n' > b.txt
cat > checks.ms <<'EOF'
false && echo WRONG || echo CONDITIONAL_OK
echo glob: *.txt
sh -c 'echo problem >&2' 2> errors.txt
cat << STOP
HEREDOC_OK
STOP
alias hi=pwd
hi
source sourced.ms
false; echo status=$?
printf abc | wc -c
EOF
printf 'echo SOURCE_OK\n' > sourced.ms
cat >> sourced.ms <<'EOF'
if false; then
echo IF_WRONG
else
echo IF_OK
fi
for item in red blue; do
echo $item
done
greet() {
echo FUNCTION_OK
}
greet
EOF
"$ROOT/mshell" checks.ms > output.txt
grep -q CONDITIONAL_OK output.txt
grep -q 'glob: a.txt b.txt' output.txt
grep -q HEREDOC_OK output.txt
grep -q SOURCE_OK output.txt
grep -q IF_OK output.txt
grep -q red output.txt
grep -q blue output.txt
grep -q FUNCTION_OK output.txt
grep -q "$TMP" output.txt
grep -q status=1 output.txt
grep -q problem errors.txt
grep -q '^3$' output.txt
"$ROOT/mshell" --trace checks.ms > trace-out.txt 2> trace.txt
grep -q '\[trace\] fork stage=' trace.txt
echo 'mshell smoke checks passed'
