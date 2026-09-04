#!/usr/bin/env bash
# End-to-end check of `retroplug-cli test` / `run` - the TypeScript harness that lets a consumer repo use
# the CLI with no Node, npm or bundler installed.
#
#   pnpm test:cli-ts        (or: tools/run-cli-ts-tests.sh)
#
# Covers what the unit tests (packages/retroplug/test/cli/tsTools.test.ts) cannot: the compiled-in
# stripper actually loading, a stripped sibling import resolving, per-file process isolation, exit-code
# aggregation, and - the one that matters most - that NON-ERASABLE syntax is REFUSED rather than emitted
# as invalid JavaScript. ts-blank-space passes an enum straight through when onError is not wired, which
# would produce a file that fails at parse time instead of a clear error.
set -uo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CLI="$REPO/build/bin/retroplug-cli"
FIX="$REPO/tools/fixtures"

if [ ! -x "$CLI" ]; then
	echo "error: $CLI not found (cmake --build build --target retroplug-cli)" >&2
	exit 2
fi

fails=0
check() { # check <label> <expected-exit> <actual-exit>
	if [ "$2" = "$3" ]; then
		echo "ok   - $1"
	else
		echo "FAIL - $1 (expected exit $2, got $3)"
		fails=$((fails + 1))
	fi
}
contains() { # contains <label> <haystack-file> <needle>
	if grep -qF -- "$3" "$2"; then
		echo "ok   - $1"
	else
		echo "FAIL - $1 (output did not contain: $3)"
		sed 's/^/       | /' "$2" >&2
		fails=$((fails + 1))
	fi
}

# Each case gets its own build dir so nothing is inherited between them.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

echo "# retroplug-cli test / run"

# 1. Filtered to the passing file: exit 0, and the stripped sibling helper must have resolved.
out="$TMP/pass.log"
"$CLI" test "$FIX/ts-harness" pass --out "$TMP/build-pass" >"$out" 2>&1
check "a passing test file exits 0" 0 $?
contains "the stripped sibling helper resolved" "$out" "a stripped sibling helper is importable"
contains "the summary counts files" "$out" "PASS: 1/1 test file(s) ok"

# 2. Whole directory: the failing file must make the run fail (the aggregate is a real gate).
out="$TMP/all.log"
"$CLI" test "$FIX/ts-harness" --out "$TMP/build-all" >"$out" 2>&1
check "a failing test file makes the run fail" 1 $?
contains "the summary reports 1 of 2" "$out" "FAIL: 1/2 test file(s) ok"

# 3. Per-file process isolation: two files, two separate TAP headers.
if [ "$(grep -c '^TAP version 13' "$out")" = "2" ]; then
	echo "ok   - each file ran in its own process (two TAP streams)"
else
	echo "FAIL - expected two TAP streams, got $(grep -c '^TAP version 13' "$out")"
	fails=$((fails + 1))
fi

# 4. Non-erasable syntax is refused, with a position - NOT emitted as invalid JavaScript.
out="$TMP/bad.log"
"$CLI" test "$FIX/ts-harness-bad" --out "$TMP/build-bad" >"$out" 2>&1
check "an enum is refused (nonzero exit)" 1 $?
contains "the refusal names the syntax" "$out" "unsupported non-erasable TypeScript syntax"
contains "the refusal carries file:line:col" "$out" "enum.test.ts:3:1"
if [ -f "$TMP/build-bad/enum.test.js" ]; then
	echo "FAIL - refused input still produced an output file"
	fails=$((fails + 1))
else
	echo "ok   - refused input produced no output file"
fi

# 5. `run` executes a single session and honours its exit code.
out="$TMP/run.log"
"$CLI" run "$FIX/ts-harness/pass.test.ts" --out "$TMP/build-run" >"$out" 2>&1
check "run executes one session (exit 0)" 0 $?
out="$TMP/run-fail.log"
"$CLI" run "$FIX/ts-harness/fail.test.ts" --out "$TMP/build-runf" >"$out" 2>&1
check "run propagates a session's failure exit" 1 $?

# 6. Stripping is position-preserving: output length must equal input length, so a stack trace from the
#    emitted .js points at the right line of the .ts.
for f in pass.test fail.test helper; do
	src="$FIX/ts-harness/$f.ts"
	dst="$TMP/build-all/$f.js"
	if [ "$(wc -c <"$src")" = "$(wc -c <"$dst")" ]; then
		echo "ok   - $f.ts stripped to the same byte length (positions preserved)"
	else
		echo "FAIL - $f.ts changed length: $(wc -c <"$src") -> $(wc -c <"$dst")"
		fails=$((fails + 1))
	fi
done

echo
if [ "$fails" = "0" ]; then
	echo "PASS: retroplug-cli TypeScript harness"
else
	echo "FAIL: $fails check(s) failed"
fi
exit $((fails == 0 ? 0 : 1))
