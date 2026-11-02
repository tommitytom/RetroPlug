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
mtime() { # mtime <file> - GNU coreutils and BSD/macOS spell this differently
	stat -c %Y "$1" 2>/dev/null || stat -f %m "$1"
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

# 5. The SDK resolves from the module REGISTRY (bare specifier, no file), and only its .d.ts is written
#    out - for editors and tsc, which read declarations off disk.
# This fixture IMPORTS the SDK, so materialization is demanded (a directory of self-contained files
# deliberately gets no SDK written next to it) and the checks below prove the written copy is usable.
SDKFIX="$TMP/sdkfix"
mkdir -p "$SDKFIX/tests"
cp "$FIX/ts-harness-sdk/tests/sdk.test.ts" "$SDKFIX/tests/"
out="$TMP/sdk1.log"
"$CLI" test "$SDKFIX/tests" >"$out" 2>&1
check "runs with no sdk/ present at all" 0 $?
contains "the materialized SDK is importable and working" "$out" "encodeWav works"
if [ -f "$SDKFIX/sdk/retroplug-cli.d.ts" ]; then
	echo "ok   - the SDK .d.ts is materialized from the binary"
else
	echo "FAIL - the .d.ts was not materialized"
	fails=$((fails + 1))
fi
if [ -e "$SDKFIX/sdk/retroplug-cli.js" ]; then
	echo "FAIL - a runtime SDK .js was written; it should resolve from the module registry"
	fails=$((fails + 1))
else
	echo "ok   - NO runtime SDK .js exists anywhere (registry-resolved)"
fi

# A stale copy (wrong content, wrong stamp) must be replaced, not trusted.
echo "corrupt" >"$SDKFIX/sdk/retroplug-cli.d.ts"
echo "deadbeefdeadbeef" >"$SDKFIX/sdk/.rp-sdk-stamp"
"$CLI" test "$SDKFIX/tests" >"$TMP/sdk2.log" 2>&1
check "a stale SDK is replaced" 0 $?
if [ "$(wc -c <"$SDKFIX/sdk/retroplug-cli.d.ts")" -gt 1000 ]; then
	echo "ok   - the stale .d.ts was rewritten from the binary"
else
	echo "FAIL - the stale SDK was left in place"
	fails=$((fails + 1))
fi

# A current copy must NOT be rewritten (no churn in the consumer's tree on every run).
before="$(mtime "$SDKFIX/sdk/retroplug-cli.d.ts")"
sleep 1
"$CLI" test "$SDKFIX/tests" >/dev/null 2>&1
if [ "$(mtime "$SDKFIX/sdk/retroplug-cli.d.ts")" = "$before" ]; then
	echo "ok   - a current .d.ts is left untouched"
else
	echo "FAIL - a current SDK was rewritten anyway"
	fails=$((fails + 1))
fi

if [ -d "$FIX/sdk" ]; then
	echo "FAIL - a self-contained fixture dir had an SDK written next to it"
	fails=$((fails + 1))
else
	echo "ok   - a directory that imports no SDK gets none written"
fi

# --out moves the stripped output, but NOT the .d.ts: nothing imports that at run time any more, and
# editors read it next to the source tests.
rm -rf "$SDKFIX/sdk"
"$CLI" test "$SDKFIX/tests" --out "$TMP/outdir/build" >"$TMP/sdk4.log" 2>&1
check "--out still runs" 0 $?
if [ -f "$SDKFIX/sdk/retroplug-cli.d.ts" ]; then
	echo "ok   - --out does not move the .d.ts (it belongs next to the SOURCE tests)"
else
	echo "FAIL - the .d.ts went missing under --out"
	fails=$((fails + 1))
fi

# 6. `run` executes a single session and honours its exit code.
out="$TMP/run.log"
"$CLI" run "$FIX/ts-harness/pass.test.ts" --out "$TMP/build-run" >"$out" 2>&1
check "run executes one session (exit 0)" 0 $?
out="$TMP/run-fail.log"
"$CLI" run "$FIX/ts-harness/fail.test.ts" --out "$TMP/build-runf" >"$out" 2>&1
check "run propagates a session's failure exit" 1 $?

# 6b. A session whose sibling build dir cannot be written (a file straight under /tmp has "/" for a
#     sibling) falls back to a temp dir instead of dying with "could not write", and says so.
RO="$TMP/ro"
mkdir -p "$RO/src"
cp "$FIX/ts-harness/pass.test.ts" "$FIX/ts-harness/helper.ts" "$RO/src/"
chmod 555 "$RO"
out="$TMP/run-ro.log"
"$CLI" run "$RO/src/pass.test.ts" >"$out" 2>&1
code=$?
chmod 755 "$RO"
check "run under an unwritable sibling still exits 0" 0 $code
contains "the fallback is announced" "$out" "sibling build dir is not writable"
contains "the session ran from the fallback" "$out" "a stripped sibling helper is importable"

# 6c. The kit sessions' async exit. A kit verb awaits its compile (offline graph jobs, settled by the launcher's
#     job loop), so the process must exit 0 only AFTER the bank is on disk, and a rejected compile must still exit 1
#     with the reason. Both go through the REAL binary and its runSession, which a direct tool call in test-native
#     does not: if runSession exited before the await settled, the exit-0 case would race the file and the rejected
#     case would exit 0 with nothing on stderr.
#
#     The source is a canonical 16-bit mono WAV written here rather than committed: a 4400-frame square wave at
#     44100 Hz, so 8800 data bytes after a fixed 44-byte header.
wav="$TMP/tone.wav"
{
	printf 'RIFF\x84\x22\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x44\xac\x00\x00\x88\x58\x01\x00\x02\x00\x10\x00data\x60\x22\x00\x00'
	for ((i = 0; i < 44; i++)); do
		printf '\x40\x1f%.0s' {1..50} # 50 frames at about +8000
		printf '\xc0\xe0%.0s' {1..50} # 50 frames at about -8000
	done
} >"$wav"
echo '{ "name": "TST", "build": [{ "file": "tone.wav", "name": "SQR" }] }' >"$TMP/kit.json"
out="$TMP/build-kit.log"
"$CLI" lsdj-rom build-kit "$TMP/kit.json" "$TMP/out.kit" >"$out" 2>&1
check "lsdj-rom build-kit exits 0 once its compile settled" 0 $?
if [ "$(wc -c <"$TMP/out.kit" 2>/dev/null)" = "16384" ]; then
	echo "ok   - the bank was on disk when the process exited (16384 B)"
else
	echo "FAIL - no 16384-byte bank after build-kit exited 0"
	fails=$((fails + 1))
fi
echo '{ "name": "TST", "build": [{ "file": "nope.wav", "name": "SQR" }] }' >"$TMP/kit-missing.json"
"$CLI" lsdj-rom build-kit "$TMP/kit-missing.json" "$TMP/out-missing.kit" >"$TMP/build-kit-missing.out" 2>"$TMP/build-kit-missing.err"
check "a rejected compile exits 1" 1 $?
contains "the refusal carries the slot's reason" "$TMP/build-kit-missing.err" "did not compile"

#     A sample the bank holds only in part keeps the bytes that fit, so the read-back check cannot see it; the CLI
#     renders the compiler's reason on stderr instead. Two copies of one 123000-frame silent source (about 32000
#     frames at the kit rate, 15984 of the sample space's 16288 bytes) leave a few hundred bytes for the second.
big="$TMP/big.wav"
{
	printf 'RIFF\x14\xc1\x03\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x44\xac\x00\x00\x88\x58\x01\x00\x02\x00\x10\x00data\xf0\xc0\x03\x00'
	head -c 246000 /dev/zero # 123000 mono 16-bit frames, all zero
} >"$big"
echo '{ "name": "CLIP", "build": [{ "file": "big.wav", "name": "A" }, { "file": "big.wav", "name": "B" }] }' >"$TMP/kit-clip.json"
"$CLI" lsdj-rom build-kit "$TMP/kit-clip.json" "$TMP/clip.kit" >"$TMP/build-kit-clip.out" 2>"$TMP/build-kit-clip.err"
check "a partly placed sample still builds the kit" 0 $?
contains "the clipped slot is warned about, with the compiler's reason" "$TMP/build-kit-clip.err" \
	"warning: sample 1 (big.wav): the kit's sample space ran out"

#     A filter flag with no --gain is the case the old RPC refused outright (`Field named 'gain' not found`), so it is
#     driven here through the real flags: the build exits 0, and the bank differs from the same spec's without the flag,
#     which is what proves the effect reached the chain rather than being dropped.
echo '{ "name": "TST", "build": [{ "file": "tone.wav", "name": "SQR" }] }' >"$TMP/kit-filter.json"
"$CLI" lsdj-rom build-kit "$TMP/kit-filter.json" "$TMP/flat.kit" >"$TMP/build-flat.log" 2>&1
check "the filter case's plain build exits 0" 0 $?
"$CLI" lsdj-rom build-kit "$TMP/kit-filter.json" "$TMP/filtered.kit" --filter LowPass --cutoff 2000 >"$TMP/build-filtered.log" 2>&1
check "lsdj-rom build-kit compiles a --filter with no --gain" 0 $?
if cmp -s "$TMP/flat.kit" "$TMP/filtered.kit"; then
	echo "FAIL - the filter flag changed no byte of the bank"
	fails=$((fails + 1))
else
	echo "ok   - the filter flag reached the chain (the bank moved)"
fi

#     --filter without --cutoff takes the compiler's 5734 Hz, the kit rate's own Nyquist, so a source at or below
#     11468 Hz is refused with a Nyquist reason: the CLI does not know the rate and the refusal is the user's call, so
#     it is documented on the --cutoff help line rather than papered over with a source-relative default. The same 8000
#     Hz source compiles once --cutoff names a frequency under its Nyquist.
low="$TMP/low.wav"
{
	printf 'RIFF\x84\x22\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x40\x1f\x00\x00\x80\x3e\x00\x00\x02\x00\x10\x00data\x60\x22\x00\x00'
	head -c 8800 /dev/zero
} >"$low"
echo '{ "name": "LOW", "build": [{ "file": "low.wav", "name": "LO" }] }' >"$TMP/kit-low.json"
"$CLI" lsdj-rom build-kit "$TMP/kit-low.json" "$TMP/low-nocut.kit" --filter LowPass >"$TMP/build-low.out" 2>"$TMP/build-low.err"
check "a --filter with no --cutoff on an 8000 Hz source is refused" 1 $?
contains "the refusal names the Nyquist frequency" "$TMP/build-low.err" "Nyquist frequency"
"$CLI" lsdj-rom build-kit "$TMP/kit-low.json" "$TMP/low-cut.kit" --filter LowPass --cutoff 2000 >"$TMP/build-lowcut.log" 2>&1
check "the same source compiles with a --cutoff under its Nyquist" 0 $?

# 6d. `render` through the REAL binary, which test-native's render-cli.test.ts (the tool inside the test host) cannot
#     reach: the launcher's unbounded pump, the exit codes, the output streams and Ctrl-C. A render is an engine job
#     (docs/modular-graph-phase-d3-design.md), so this is also the check that the binary's files are the render
#     baseline's: the fixture-free smsggdj case, run from a RELATIVE working directory with relative arguments, must
#     write the golden's bytes (the hash on the baseline's host, Linux x86_64 here; the size anywhere).
REPO_ROM="$REPO/resources/roms/smsggdj_v0_45.sms"
GOLDEN="$REPO/packages/retroplug/test-native/golden/render-baseline.json"
if [ -f "$REPO_ROM" ] && [ -f "$GOLDEN" ]; then
	RD="$TMP/render"
	mkdir -p "$RD"
	cp "$REPO_ROM" "$RD/smsggdj.sms"
	cp "$FIX/render-smsggdj.sav" "$RD/metro.sav"
	out="$TMP/render.out"
	err="$TMP/render.err"
	(cd "$RD" && RETROPLUG_DETERMINISTIC=1 "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 4s --out metro.wav >"$out" 2>"$err")
	check "render of a relative path exits 0" 0 $?
	contains "render prints the song it loaded" "$out" 'song "METRO" (slot 0)'
	contains "render prints its announcement with the paths as typed" "$out" "rendering smsggdj.sms → metro.wav (4000ms)…"
	contains "render prints the file it wrote" "$out" "metro.wav"
	contains "render prints its summary" "$out" "rendered smsggdj.sms → metro.wav (@44100Hz)"
	if [ -s "$err" ]; then
		echo "FAIL - a clean render wrote to stderr"
		sed 's/^/       | /' "$err" >&2
		fails=$((fails + 1))
	else
		echo "ok   - a clean render writes nothing to stderr"
	fi
	want="$(python3 - "$GOLDEN" <<'PY'
import json, sys
c = json.load(open(sys.argv[1]))["cases"]["smsggdj/sms/index-0/mix"]["files"][0]
print(c["hash"], c["size"])
PY
)"
	got="$(python3 - "$RD/metro.wav" <<'PY'
import sys
h = 0xcbf29ce484222325
d = open(sys.argv[1], "rb").read()
for b in d:
    h = ((h ^ b) * 0x100000001b3) & 0xFFFFFFFFFFFFFFFF
print("%016x" % h, len(d))
PY
)"
	if [ "${got#* }" = "${want#* }" ]; then
		echo "ok   - the CLI's WAV is the golden's size (${got#* } B)"
	else
		echo "FAIL - the CLI's WAV is ${got#* } B, the golden's is ${want#* } B"
		fails=$((fails + 1))
	fi
	if [ "$(uname -sm)" = "Linux x86_64" ]; then
		if [ "${got%% *}" = "${want%% *}" ]; then
			echo "ok   - the CLI's WAV hashes to the golden's (${got%% *})"
		else
			echo "FAIL - the CLI's WAV hashes to ${got%% *}, the golden's is ${want%% *}"
			fails=$((fails + 1))
		fi
	fi

	# A failed render and a usage error exit 1 with their messages, naming the path as typed.
	(cd "$RD" && "$CLI" render nope.sms --duration 1s >"$TMP/render-bad.out" 2>"$TMP/render-bad.err")
	check "render of a missing ROM exits 1" 1 $?
	contains "the missing-ROM message names the path as typed" "$TMP/render-bad.err" "ERROR: render: could not load ROM: nope.sms"
	(cd "$RD" && "$CLI" render smsggdj.sms --split sideways >"$TMP/render-use.out" 2>"$TMP/render-use.err")
	check "render with a bad flag value exits 1" 1 $?
	contains "the usage message says what was wrong" "$TMP/render-use.err" "ERROR: render: --split must be one of mix|channels|pins (got sideways)"

	# Ctrl-C mid-render cancels the job: prompt, non-zero, and no WAV left behind (a cancelled render's header would
	# claim no audio, so the library removes what it opened). A 600 s cap is ~30 s of work, so it is still running.
	(cd "$RD" && exec "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 600s --out cut.wav >"$TMP/render-int.out" 2>"$TMP/render-int.err") &
	pid=$!
	for _ in $(seq 1 100); do
		[ -f "$RD/cut.wav" ] && break
		sleep 0.1
	done
	sleep 1
	started=$SECONDS
	kill -INT "$pid" 2>/dev/null
	wait "$pid"
	code=$?
	if [ "$code" != "0" ] && [ "$code" != "" ]; then
		echo "ok   - an interrupted render exits non-zero ($code)"
	else
		echo "FAIL - an interrupted render exited $code"
		fails=$((fails + 1))
	fi
	if [ $((SECONDS - started)) -le 10 ]; then
		echo "ok   - the interrupt stopped the render promptly"
	else
		echo "FAIL - the interrupted render took $((SECONDS - started)) s to stop"
		fails=$((fails + 1))
	fi
	contains "an interrupted render reports it was cancelled" "$TMP/render-int.err" "ERROR: render: cancelled"
	if [ -e "$RD/cut.wav" ]; then
		echo "FAIL - an interrupted render left $RD/cut.wav behind"
		fails=$((fails + 1))
	else
		echo "ok   - an interrupted render left no WAV whose header claims no audio"
	fi
else
	echo "skip - render leg: the smsggdj ROM or the render golden is absent"
fi

# 7. Stripping is position-preserving: output length must equal input length, so a stack trace from the
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
