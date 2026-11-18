#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT HUP INT TERM
cat > "$TMP/case.ms" <<'EOF'
echo compatible
printf abc | wc -c
false && echo skipped || echo recovered
EOF
"$ROOT/mshell" "$TMP/case.ms" > "$TMP/mshell.out"
bash "$TMP/case.ms" > "$TMP/bash.out"
diff -u "$TMP/bash.out" "$TMP/mshell.out"
echo 'supported-subset Bash compatibility checks passed'
