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
# The build's CLI. RETROPLUG_CLI names another, which is how section 6h's exit cases are also run against an assertions
# build (build-dbg): a JS reference the exit paths leak aborts JS_FreeRuntime's gc_obj_list check there, at exit, where
# the release build cannot see it.
CLI="${RETROPLUG_CLI:-$REPO/build/bin/retroplug-cli}"
FIX="$REPO/tools/fixtures"

if [ ! -x "$CLI" ]; then
	echo "error: $CLI not found (cmake --build build --target retroplug-cli)" >&2
	exit 2
fi
# Absolute, so a relative RETROPLUG_CLI still names the binary in the cases that change directory (6d's renders).
CLI="$(cd "$(dirname "$CLI")" && pwd)/$(basename "$CLI")"

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

contains_line() { # contains_line <label> <haystack-file> <exact line> - a result line, so "ok 2" is not "not ok 2"
	if grep -qxF -- "$3" "$2"; then
		echo "ok   - $1"
	else
		echo "FAIL - $1 (no line equal to: $3)"
		sed 's/^/       | /' "$2" >&2
		fails=$((fails + 1))
	fi
}

quiet() { # quiet <label> <stderr-file> - nothing was written to it
	if [ ! -s "$2" ]; then
		echo "ok   - $1"
	else
		echo "FAIL - $1 (stderr was not empty)"
		sed 's/^/       | /' "$2" >&2
		fails=$((fails + 1))
	fi
}
absent() { # absent <label> <haystack-file> <needle> - and it is NOT in there
	if grep -qF -- "$3" "$2"; then
		echo "FAIL - $1 (output contained: $3)"
		sed 's/^/       | /' "$2" >&2
		fails=$((fails + 1))
	else
		echo "ok   - $1"
	fi
}

# Each case gets its own build dir so nothing is inherited between them.
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
export RETROPLUG_PACKS_DIR="$TMP/packs" RETROPLUG_PACKS_CACHE_DIR="$TMP/packs-cache"; unset RETROPLUG_PACK_PATH

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

# 5b. A TEST file that calls runSession is refused: runSession boots the control plane and exits the process, so a test
#     file using it would end the run before the harness printed, which the runner would read as a pass (exit 0).
#     Copied to a temp project as step 5's fixture is, so the SDK's .d.ts the fixture demands is written beside its
#     tests there and not into the repository's fixtures tree.
RSFIX="$TMP/runsession"
mkdir -p "$RSFIX/tests"
cp "$FIX/ts-harness-run-session/run-session.test.ts" "$RSFIX/tests/"
out="$TMP/run-session.log"
"$CLI" test "$RSFIX/tests" >"$out" 2>&1
check "a test file calling runSession fails the run" 1 $?
contains "the refusal says what to use instead" "$out" "runSession is for a session file run by path"

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
#     write the golden's bytes (the hash on the golden's build target, which the binary names; the size anywhere).
REPO_ROM="$REPO/resources/roms/smsggdj_v0_45.sms"
GOLDEN="$REPO/packages/retroplug/test-native/golden/render-baseline.json"
if [ -f "$REPO_ROM" ] && [ -f "$GOLDEN" ]; then
	RD="$TMP/render"
	mkdir -p "$RD"
	cp "$REPO_ROM" "$RD/smsggdj.sms"
	cp "$FIX/render-smsggdj.sav" "$RD/metro.sav"
	out="$TMP/render.out"
	err="$TMP/render.err"
	(cd "$RD" && RETROPLUG_DETERMINISTIC=1 "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 4s \
		--out metro.wav >"$out" 2>"$err")
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
	# Exact bytes are compared only on the golden's own build target, which the binary names (the same string the
	# test-native suites key on); a Linux x86_64 build by another compiler agrees on the size and not on a hash.
	# RP_RENDER_GOLDEN_REQUIRE_TARGET=1 (CI's linux-x64 job) turns that skip into a failure.
	cat >"$TMP/build-target.ts" <<'TS'
import { bootSession } from "retroplug-cli";
console.log("build-target " + bootSession().audio.buildTarget());
(globalThis as unknown as { tjs: { exit(code: number): void } }).tjs.exit(0);
TS
	have_target="$("$CLI" run "$TMP/build-target.ts" --out "$TMP/build-target" 2>/dev/null | sed -n 's/^build-target //p')"
	want_target="$(python3 -c 'import json, sys; print(json.load(open(sys.argv[1]))["target"])' "$GOLDEN")"
	if [ -z "$have_target" ]; then
		# The binary always names its target, so a probe that printed none is a broken probe, not a foreign build.
		echo "FAIL - the CLI's WAV hash: the binary did not name its build target"
		fails=$((fails + 1))
	elif [ "$have_target" = "$want_target" ]; then
		if [ "${got%% *}" = "${want%% *}" ]; then
			echo "ok   - the CLI's WAV hashes to the golden's (${got%% *})"
		else
			echo "FAIL - the CLI's WAV hashes to ${got%% *}, the golden's is ${want%% *}"
			fails=$((fails + 1))
		fi
	elif [ "${RP_RENDER_GOLDEN_REQUIRE_TARGET:-}" = "1" ]; then
		echo "FAIL - the CLI's WAV hash: this build is $have_target, the golden's target is $want_target"
		fails=$((fails + 1))
	else
		echo "skip - the CLI's WAV hash: this build is $have_target, the golden's target is $want_target"
	fi

	# A failed render and a usage error exit 1 with their messages, naming the path as typed.
	(cd "$RD" && "$CLI" render nope.sms --duration 1s >"$TMP/render-bad.out" 2>"$TMP/render-bad.err")
	check "render of a missing ROM exits 1" 1 $?
	contains "the missing-ROM message names the path as typed" "$TMP/render-bad.err" \
		"ERROR: render: could not load ROM: nope.sms"
	(cd "$RD" && "$CLI" render smsggdj.sms --split sideways >"$TMP/render-use.out" 2>"$TMP/render-use.err")
	check "render with a bad flag value exits 1" 1 $?
	contains "the usage message says what was wrong" "$TMP/render-use.err" \
		"ERROR: render: --split must be one of mix|channels|pins (got sideways)"

	# Ctrl-C mid-render cancels the job: prompt, exit 1 (not 130, a signal death, or 0), and no WAV left behind (the
	# library streams to a cut.wav.<token>.part of its own and removes it). A 600 s cap is ~30 s of work, so it is still
	# running.
	(cd "$RD" && exec "$CLI" render smsggdj.sms --sav metro.sav --song-index 0 --duration 600s --out cut.wav \
		>"$TMP/render-int.out" 2>"$TMP/render-int.err") &
	pid=$!
	for _ in $(seq 1 100); do
		compgen -G "$RD/cut.wav.*.part" >/dev/null && break
		sleep 0.1
	done
	sleep 1
	started=$SECONDS
	kill -INT "$pid" 2>/dev/null
	wait "$pid"
	code=$?
	if [ "$code" = "1" ]; then
		echo "ok   - an interrupted render exits 1"
	else
		echo "FAIL - an interrupted render exited $code, not 1"
		fails=$((fails + 1))
	fi
	if [ $((SECONDS - started)) -le 10 ]; then
		echo "ok   - the interrupt stopped the render promptly"
	else
		echo "FAIL - the interrupted render took $((SECONDS - started)) s to stop"
		fails=$((fails + 1))
	fi
	contains "an interrupted render reports it was cancelled" "$TMP/render-int.err" "ERROR: render: cancelled"
	if [ -e "$RD/cut.wav" ] || compgen -G "$RD/cut.wav.*.part" >/dev/null; then
		echo "FAIL - an interrupted render left cut.wav or a cut.wav.*.part behind"
		fails=$((fails + 1))
	else
		echo "ok   - an interrupted render left no WAV whose header claims no audio"
	fi
else
	echo "skip - render leg: the smsggdj ROM or the render golden is absent"
fi

# 6e. The harness RECORDS a rejection nothing handled, rather than leaving it to the runtime: uncancelled, txiki makes
#     it fatal, and whether a given leak killed the file with no TAP at all (every other case lost) or passed unseen
#     depended on whether it was dispatched before the harness had exited. A rejection handled a loop turn late counts,
#     as in Node.
RJ="$TMP/rejections"
mkdir -p "$RJ/src"
cat >"$RJ/src/late.test.ts" <<'EOF'
import { test, keepAlive } from "retroplug-cli";
keepAlive();
test("handles its rejection a turn late", async () => {
  const p = Promise.reject(new Error("late-handled"));
  await new Promise((r) => setTimeout(r, 1));
  await p.catch(() => {});
});
test("a clean case after it", () => {});
EOF
cat >"$RJ/src/skips.test.ts" <<'EOF'
import { test, skip } from "retroplug-cli";
test("skips but leaves a rejection", () => {
  void Promise.reject(new Error("left by the skipper"));
  skip("no fixture");
});
test("innocent after the skipper", () => {});
EOF
cat >"$RJ/src/outside.test.ts" <<'EOF'
import { test } from "retroplug-cli";
test("registered first", () => {});
Promise.reject(new Error("stray rejection at top level"));
EOF
out="$TMP/test-rj.log"
timeout 30 "$CLI" test "$RJ/src" -j1 >"$out" 2>&1
check "files with stray rejections fail the run" 1 $?
contains_line "a late-handled rejection fails ITS case" "$out" "not ok 1 - handles its rejection a turn late"
contains_line "...and the file's other cases still report" "$out" "ok 2 - a clean case after it"
contains "a rejection outside every case fails the file, saying so" "$out" "outside any test"
contains "a file whose only error is outside every case fails on its own" "$out" \
	"failed: late.test.js, outside.test.js, skips.test.js"
#     A case that skips, or throws, still owns the rejection it left: the next case is innocent.
contains_line "a case that skips with a rejection pending is not reported as a skip" "$out" \
	"not ok 1 - skips but leaves a rejection"
contains_line "...and the case after it is not blamed" "$out" "ok 2 - innocent after the skipper"

# 6f. Recording an error is not waiting for it. A case awaiting work that died first (a background task that threw
#     before signalling) used to wait for ever, since the record was read only once the case returned, which under
#     keepAlive() hung the run; and a timer's throw was blamed on whichever case was running when it fired. `timeout`
#     turns a regression into a 124 rather than a hung suite.
RW="$TMP/recorded"
mkdir -p "$RW/src"
cat >"$RW/src/dies.test.ts" <<'EOF'
import { test, keepAlive } from "retroplug-cli";
keepAlive();
test("awaits a background task that dies before signalling", async () => {
  let done!: () => void;
  const finished = new Promise<void>((r) => (done = r));
  void (async () => {
    await Promise.resolve();
    throw new Error("background task died");
    done();
  })();
  await finished;
});
test("a clean case after it", () => {});
EOF
cat >"$RW/src/blame.test.ts" <<'EOF'
import { test, keepAlive } from "retroplug-cli";
keepAlive();
test("leaves a timer that throws later", () => {
  setTimeout(() => {
    throw new Error("from the first test");
  }, 20);
});
test("innocent, and busy when that timer fires", async () => {
  await new Promise((r) => setTimeout(r, 60));
});
EOF
out="$TMP/test-rw.log"
timeout 30 "$CLI" test "$RW/src" -j1 >"$out" 2>&1
check "a case awaiting a dead task fails the run instead of hanging it" 1 $?
contains_line "...and fails THAT case" "$out" "not ok 1 - awaits a background task that dies before signalling"
contains "...with the reason" "$out" "background task died"
contains_line "...and the file's other cases still report" "$out" "ok 2 - a clean case after it"
contains_line "a timer's throw does not fail the innocent case running when it fires" "$out" \
	"ok 2 - innocent, and busy when that timer fires"
contains "...it is blamed on the case that created it" "$out" "from test 1, after it had finished"
contains "a file whose only error is a finished case's timer fails on its own" "$out" "failed: blame.test.js, dies.test.js"
contains "...and its TAP block names the case the timer came from" "$out" \
	"#   uncaught error in a timer (from test 1, after it had finished): from the first test"

# 6g. An error nothing caught ends a session, kept alive or not, with exit 1 and its text. txiki turns one (a throw in
#     an async module body, a rejection nothing cancelled, a throw in a timer) into a pending exception and a request to
#     stop ITS loop, and the launcher drives the loop itself, so it looks after each pump: a plain session says what
#     threw rather than only that it did not complete, a kept-alive one stops rather than pumping for ever (under `test`
#     it would wedge the whole run), and a timer's throw is not lost with exit 0. Ctrl-C on a kept-alive session with no
#     interrupt hook is a clean exit 0, and a hook that throws ends the session.
UC="$TMP/uncaught"
mkdir -p "$UC/src"
printf 'import { keepAlive } from "retroplug-cli";\nkeepAlive();\nthrow new Error("setup bug after keepAlive");\n' \
	>"$UC/src/ka.test.ts"
out="$TMP/run-ka.log"
timeout 20 "$CLI" run "$UC/src/ka.test.ts" >"$out" 2>&1
check "an uncaught error after keepAlive() exits 1, not a hang" 1 $?
contains "...and prints the error" "$out" "setup bug after keepAlive"
out="$TMP/test-ka.log"
timeout 20 "$CLI" test "$UC/src" >"$out" 2>&1
check "a test file that throws after keepAlive() fails the run, not hangs it" 1 $?
printf 'throw new Error("plain setup bug");\n' >"$UC/plain.ts"
out="$TMP/run-plain.log"
timeout 20 "$CLI" run "$UC/plain.ts" >"$out" 2>&1
check "an uncaught error without keepAlive() exits 1" 1 $?
contains "...and prints the error, not only that the session did not complete" "$out" "plain setup bug"
printf '%s\n' 'declare const tjs: { exit(code: number): void };' 'import { keepAlive } from "retroplug-cli";' \
	'keepAlive();' 'setTimeout(() => {' '  throw new Error("thrown in a timer");' '}, 5);' \
	'setTimeout(() => tjs.exit(0), 200);' >"$UC/timer.ts"
out="$TMP/run-timer.log"
timeout 20 "$CLI" run "$UC/timer.ts" >"$out" 2>&1
check "a timer's throw ends the session with exit 1, not a lost error and exit 0" 1 $?
contains "...and prints the error" "$out" "thrown in a timer"
#     An error a sibling callback's try/catch cleared before it was reported: the session ran on and exited 0. txiki's
#     stop request survives where the exception does not.
printf '%s\n' 'declare const tjs: { exit(code: number): void };' 'import { keepAlive } from "retroplug-cli";' \
	'keepAlive();' 'setTimeout(() => {' '  throw new Error("lost");' '}, 5);' 'setTimeout(() => {' \
	'  try {' '    JSON.parse("{");' '  } catch {}' '}, 5);' 'setTimeout(() => tjs.exit(0), 200);' >"$UC/lost.ts"
out="$TMP/run-lost.log"
timeout 20 "$CLI" run "$UC/lost.ts" >"$out" 2>&1
check "an error cleared by a sibling callback still ends the session (exit 1)" 1 $?
contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
#     The same without keepAlive(): the bounded loop's own cleared-error path.
printf '%s\n' 'setTimeout(() => {' '  throw new Error("lost");' '}, 5);' 'setTimeout(() => {' '  try {' \
	'    JSON.parse("{");' '  } catch {}' '}, 5);' >"$UC/lost-bounded.ts"
out="$TMP/run-lost-bounded.log"
timeout 20 "$CLI" run "$UC/lost-bounded.ts" >"$out" 2>&1
check "an error cleared by a sibling callback ends a bounded session too (exit 1)" 1 $?
contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
wait_ready() {
	for _ in $(seq 1 100); do
		grep -q '^ready$' "$1" 2>/dev/null && return 0
		sleep 0.1
	done
}
#     Ctrl-C on a kept-alive session, as a `.js` file the launcher runs itself (`run` on a `.ts` runs it in a child,
#     which a signal to the parent never reaches): no hook is a clean exit 0; a hook that throws synchronously ends it
#     with exit 1.
# A 0 ms timer fires only once the loop is pumping, after the launcher has installed its SIGINT handler: the script
# prints a marker from one, and the signal waits for it.
printf '%s\n' 'globalThis.__rp_keepAlive();' 'setTimeout(() => console.log("ready"), 0);' >"$UC/ka-idle.js"
timeout 20 "$CLI" "$UC/ka-idle.js" >"$TMP/run-ka-idle.log" 2>&1 &
pid=$!
wait_ready "$TMP/run-ka-idle.log"
kill -INT "$pid" 2>/dev/null
wait "$pid"
check "Ctrl-C on a kept-alive session with no interrupt hook exits 0" 0 $?
printf '%s\n' 'globalThis.__rp_keepAlive();' \
	'globalThis.__rp_onInterrupt(() => {' '  throw new Error("hook threw");' '});' \
	'setTimeout(() => console.log("ready"), 0);' >"$UC/ka-hook.js"
out="$TMP/run-ka-hook.log"
timeout 20 "$CLI" "$UC/ka-hook.js" >"$out" 2>&1 &
pid=$!
wait_ready "$out"
kill -INT "$pid" 2>/dev/null
wait "$pid"
check "a throwing interrupt hook ends a kept-alive session with exit 1" 1 $?
contains "...and prints the error" "$out" "hook threw"
#     A hook that ENDS the session itself (release the devices, then exit with the code a wrapper expects): the session
#     ends with the hook's code, and the marker that carried the unwinding out of tjs.exit is not an error to report.
printf '%s\n' 'globalThis.__rp_keepAlive();' \
	'globalThis.__rp_onInterrupt(() => {' '  tjs.exit(5);' '});' \
	'setTimeout(() => console.log("ready"), 0);' >"$UC/ka-exit.js"
out="$TMP/run-ka-exit.log"
timeout 20 "$CLI" "$UC/ka-exit.js" >"$out" 2>&1 &
pid=$!
wait_ready "$out"
kill -INT "$pid" 2>/dev/null
wait "$pid"
check "an interrupt hook that calls tjs.exit ends the session with its code" 5 $?
absent "...and never shows the exit's marker" "$out" "taken by the embedder's exit hook"

# 6h. tjs.exit returns to the launcher. txiki's exit called C exit() from inside the JS call, so main's frame (the
#     Teardown, the providers, the runtime) never unwound on a NORMAL exit - the launchpad a session claimed never got
#     its close bytes - and an error thrown in the same turn as a tjs.exit(0) was lost with exit 0. With txiki's exit
#     hook the exit unwinds like a thrown session or a Ctrl-C, so the same teardown runs, and the hook records what was
#     pending when exit was called. RETROPLUG_TEARDOWN_TRACE=1 makes the teardown say it ran.
#     The reference counts on these paths are checked by building this script against an assertions build:
#     `RETROPLUG_CLI=build-dbg/bin/retroplug-cli tools/run-cli-ts-tests.sh`, where a rejection's reason an exit path
#     forgot to free aborts JS_FreeRuntime's gc_obj_list check at exit (rc 134) instead of exiting 2. The release binary
#     cannot see that (quickjs-ng asserts only off NDEBUG), so the listener case below is a guard only in that run.
EX="$TMP/exit"
mkdir -p "$EX"
printf '%s\n' 'tjs.exit(0);' >"$EX/normal.js"
out="$TMP/run-exit-normal.log"
RETROPLUG_TEARDOWN_TRACE=1 timeout 20 "$CLI" "$EX/normal.js" >"$out" 2>&1
check "a normal tjs.exit(0) still exits 0" 0 $?
contains "...and runs the launcher's teardown first" "$out" "teardown: graph cleared, providers stopped"
printf '%s\n' 'tjs.exit(3);' >"$EX/three.js"
out="$TMP/run-exit-three.log"
timeout 20 "$CLI" "$EX/three.js" >"$out" 2>&1
check "tjs.exit(3) still exits 3" 3 $?
#     Two 0 ms timers in one loop turn: the first throws, the second asks for a clean exit. The hook is told an exception
#     was already pending, so the session fails with that error's text instead of exiting 0 silently.
printf '%s\n' 'setTimeout(() => {' '  throw new Error("thrown before the exit");' '}, 0);' \
	'setTimeout(() => tjs.exit(0), 0);' >"$EX/same-turn.js"
out="$TMP/run-exit-same-turn.log"
timeout 20 "$CLI" "$EX/same-turn.js" >"$out" 2>&1
check "an error thrown in the same turn as tjs.exit(0) exits 1" 1 $?
contains "...and prints the error" "$out" "thrown before the exit"
#     An error a SIBLING callback cleared in the same turn: the exception is gone, so the exit is the one place that can
#     still know something threw. txiki exposes the stop request, and the hook asks it.
printf '%s\n' 'setTimeout(() => {' '  throw new Error("lost before the exit");' '}, 0);' \
	'setTimeout(() => {' '  try {' '    JSON.parse("{");' '  } catch {}' '}, 0);' \
	'setTimeout(() => tjs.exit(0), 0);' >"$EX/same-turn-cleared.js"
out="$TMP/run-exit-cleared.log"
timeout 20 "$CLI" "$EX/same-turn-cleared.js" >"$out" 2>&1
check "an error a sibling cleared in the same turn as tjs.exit(0) exits 1" 1 $?
contains "...and says why" "$out" "a callback threw, and its error was cleared before it could be reported"
absent "...and the exit's own marker never reaches the log" "$out" "taken by the embedder's exit hook"
#     The DISPATCHER's own synchronous exits. cli/cli.ts calls tjs.exit from the module body for --version, the bare
#     index, an unknown command and a tool's --help, and cli/session.ts's runSession calls it for a synchronous tool
#     (lsdj-rom info with no argument falls into tjs.exit(1)). With the hook each of those exits unwinds through the
#     module's promise, and none of them is a failed session: the marker IS the unwinding, not an error.
timeout 20 "$CLI" --version >"$EX/version.out" 2>"$EX/version.err"
check "--version exits 0" 0 $?
quiet "...and says nothing on stderr" "$EX/version.err"
timeout 20 "$CLI" --help >"$EX/help.out" 2>"$EX/help.err"
check "--help exits 0" 0 $?
quiet "...and says nothing on stderr" "$EX/help.err"
timeout 20 "$CLI" >"$EX/bare.out" 2>"$EX/bare.err"
check "a bare call exits 2" 2 $?
absent "...and does not report a failed session" "$EX/bare.err" "session eval failed"
timeout 20 "$CLI" no-such-command >"$EX/unknown.out" 2>"$EX/unknown.err"
check "an unknown command exits 2" 2 $?
absent "...and does not report a failed session" "$EX/unknown.err" "session eval failed"
timeout 20 "$CLI" lsdj-rom info >"$EX/tool.out" 2>"$EX/tool.err"
check "a synchronous tool's own exit code stands (lsdj-rom info with no argument: 1)" 1 $?
contains "...and prints its usage" "$EX/tool.err" "usage: lsdj-rom info"
absent "...and does not report a failed session" "$EX/tool.err" "session eval failed"
absent "...and never shows the exit's marker" "$EX/tool.err" "taken by the embedder's exit hook"
#     An exit with a microtask queued behind it: nothing may run JS once the exit has been taken, so the job never runs
#     (before the fork honoured that, it did, and its side effects were the session's last word).
printf '%s\n' 'setTimeout(() => {' '  Promise.resolve().then(() => console.log("ran after exit"));' \
	'  tjs.exit(0);' '}, 0);' >"$EX/after-exit.js"
out="$TMP/run-exit-after.log"
timeout 20 "$CLI" "$EX/after-exit.js" >"$out" 2>&1
check "a clean exit after a queued microtask still exits 0" 0 $?
absent "...and the job queued behind it never ran" "$out" "ran after exit"
#     An exit the ENGINE swallows: quickjs-ng takes a promise executor's exception with JS_GetException and without the
#     uncatchable check, so the job that called tjs.exit returns normally and JS_ExecutePendingJob reports it done. The
#     drain checks the exit on every pass, so the job queued behind that one still does not run.
printf '%s\n' 'Promise.resolve().then(() => { new Promise(() => tjs.exit(0)); });' \
	'Promise.resolve().then(() => console.log("ran after exit"));' >"$EX/swallowed.js"
out="$TMP/run-exit-swallowed.log"
timeout 20 "$CLI" "$EX/swallowed.js" >"$out" 2>&1
check "an exit the engine swallows still exits 0" 0 $?
absent "...and the job queued behind it never ran either" "$out" "ran after exit"
#     A listener that ends the session ITSELF: its exit's uncatchable marker is the unwinding, not an error, so the
#     listener's status stands and nothing prints the marker. Both listeners a session can install - a rejection's and
#     the module's load event - go through a path that used to dump it.
printf '%s\n' 'addEventListener("unhandledrejection", () => tjs.exit(2));' \
	'Promise.reject(new Error("nothing caught this"));' >"$EX/listener-exit.js"
out="$TMP/run-exit-listener.log"
timeout 20 "$CLI" "$EX/listener-exit.js" >"$out" 2>&1
check "an unhandledrejection listener that exits ends the session with its code" 2 $?
quiet "...and the exit's marker is not printed as an error" "$out"
#     A listener that HANDLES a rejection: e.promise.catch() (or a .catch on one still queued) makes quickjs-ng tell the
#     tracker the promise is handled, which removes and frees that promise's entry. The dispatch used to walk the list in
#     place, so it then read, unlinked and freed the entry it was dispatching again (a segfault here), or held a pointer
#     to the next one; the fork's dispatch now takes each entry off the list before its event goes out.
printf '%s\n' '__rp_keepAlive();' 'addEventListener("unhandledrejection", (e) => {' \
	'  e.preventDefault();' '  e.promise.catch(() => {});' \
	'  setTimeout(() => { console.log("handled in its own listener"); tjs.exit(0); }, 100);' '});' \
	'Promise.reject(new Error("caught by its listener"));' >"$EX/listener-catch.js"
out="$TMP/run-listener-catch.log"
timeout 20 "$CLI" "$EX/listener-catch.js" >"$out" 2>&1
check "a listener that catches its own event's promise and cancels it goes on" 0 $?
contains "...and the session ran to its own exit" "$out" "handled in its own listener"
printf '%s\n' 'addEventListener("unhandledrejection", (e) => { e.promise.catch(() => {}); });' \
	'Promise.reject(new Error("caught but not cancelled"));' >"$EX/listener-catch-uncancelled.js"
out="$TMP/run-listener-catch-uncancelled.log"
timeout 20 "$CLI" "$EX/listener-catch-uncancelled.js" >"$out" 2>&1
check "a listener that catches its event's promise but does not cancel it aborts on the reason" 1 $?
contains "...and prints the reason" "$out" "Error: caught but not cancelled"
printf '%s\n' '__rp_keepAlive();' 'const seen = [];' 'Promise.reject(new Error("first"));' \
	'const second = Promise.reject(new Error("second"));' 'addEventListener("unhandledrejection", (e) => {' \
	'  seen.push(e.reason.message);' '  e.preventDefault();' '  second.catch(() => {});' \
	'  setTimeout(() => { console.log("events: " + seen.join(" ")); tjs.exit(0); }, 100);' '});' \
	>"$EX/listener-catch-later.js"
out="$TMP/run-listener-catch-later.log"
timeout 20 "$CLI" "$EX/listener-catch-later.js" >"$out" 2>&1
check "a listener that catches a rejection still queued behind its own goes on" 0 $?
contains_line "...and the one it caught is never dispatched" "$out" "events: first"
printf '%s\n' 'addEventListener("unhandledrejection", (e) => { e.promise.catch(() => {}); tjs.exit(3); });' \
	'Promise.reject(new Error("caught, then exited"));' >"$EX/listener-catch-exit.js"
out="$TMP/run-listener-catch-exit.log"
timeout 20 "$CLI" "$EX/listener-catch-exit.js" >"$out" 2>&1
check "a listener that catches its event's promise and then exits ends with its code" 3 $?
quiet "...and prints nothing" "$out"
#     A rejection a listener queues waits for the NEXT pass (as one queued behind the last entry always did): a listener
#     that queues one on every event must not keep a single pass, and so the loop's timers, from ever ending.
printf '%s\n' '__rp_keepAlive();' 'let events = 0;' 'addEventListener("unhandledrejection", (e) => {' \
	'  events++;' '  e.preventDefault();' '  Promise.reject(new Error("again"));' '});' \
	'Promise.reject(new Error("first"));' \
	'setTimeout(() => { console.log(events > 0 ? "the loop still turns" : "no event"); tjs.exit(0); }, 50);' \
	>"$EX/listener-requeue.js"
out="$TMP/run-listener-requeue.log"
timeout 20 "$CLI" "$EX/listener-requeue.js" >"$out" 2>&1
check "a listener that queues a rejection on every event does not hold the loop" 0 $?
contains "...and the loop's timer still fired after an event" "$out" "the loop still turns"
printf '%s\n' 'addEventListener("load", () => tjs.exit(4));' >"$EX/load-exit.js"
out="$TMP/run-exit-load.log"
timeout 20 "$CLI" "$EX/load-exit.js" >"$out" 2>&1
check "a load listener that exits ends the session with its code" 4 $?
quiet "...and the exit's marker is not printed as an error" "$out"
#     A status that is not a number (a Symbol throws the conversion): the TypeError is the conversion's, not a session
#     error the exit was asked for, so it is not reported as one. The stock exit(-1) status stands, 255 to a shell.
printf '%s\n' 'tjs.exit(Symbol());' >"$EX/symbol-exit.js"
out="$TMP/run-exit-symbol.log"
timeout 20 "$CLI" "$EX/symbol-exit.js" >"$out" 2>&1
check "an exit whose argument is not a number exits -1 (255)" 255 $?
quiet "...and the conversion's TypeError is not reported as a session error" "$out"

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

# 8. Packs (docs/downloadable-packs-m2c-plan.md): the TS fixture pack's tool is a command, and the broken packs beside
#    it are refused with their reasons while the CLI goes on. The fixture is built by retroplug-ts-fixture-packs.
PACKS="$REPO/build/packs"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) PSEP=";" ;; *) PSEP=":" ;; esac
PACKPATH="$PACKS/tsfix/1.0.0${PSEP}$PACKS/tsfix-cliclash/1.0.0${PSEP}$PACKS/tsfix-undeclared/1.0.0"
PACKPATH="$PACKPATH${PSEP}$PACKS/tsfix-dependent/1.0.0${PSEP}$PACKS/tsfix-throws/1.0.0${PSEP}$PACKS/tsfix-protocol/1.0.0"
PACKPATH="$PACKPATH${PSEP}$PACKS/tsfix-proto/1.0.0${PSEP}$PACKS/tsfix-proto-ports/1.0.0"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" --help >"$TMP/packs-help.out" 2>"$TMP/packs-help.err"
check "packs: --help with the fixture packs" 0 $?
contains "packs: a pack's tool is in the index" "$TMP/packs-help.out" "tsfix-hello"
contains "packs: a tool named after a built-in is refused" "$TMP/packs-help.err" \
	"[packs] pack tsfix-cliclash is refused:"
contains "packs: a role its manifest does not declare is refused" "$TMP/packs-help.err" \
	"contributes the role kind tsfix-undeclared, which its pack.json's provides.roles does not declare"
contains "packs: a pack requiring a refused pack is refused" "$TMP/packs-help.err" \
	"it requires the pack tsfix-throws, which is refused"
contains "packs: a protocol half with no protocol member is refused" "$TMP/packs-help.err" \
	"halves.protocol needs the protocol member"
contains "packs: a protocol node whose ports are not its wiring is refused in TS" "$TMP/packs-help.err" \
	"[packs] pack tsfix-proto-ports is refused: its protocol node rp.tsfix-proto-ports.echo has the ports"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" tsfix-hello >"$TMP/packs-run.out" 2>/dev/null
check "packs: a pack's tool runs" 0 $?
contains "packs: the tool's output" "$TMP/packs-run.out" "hello from the ts fixture pack"
RETROPLUG_PACK_PATH="$PACKPATH" "$CLI" render --help >/dev/null 2>&1
check "packs: a built-in command still routes beside them" 0 $?

# A render job's worker loads the packs' app and consoles halves (docs/downloadable-packs-m2d-plan.md): the fixture's
# role reaches the system the job builds, and adds nothing to its bytes.
if [ -f "$REPO_ROM" ]; then
	RR="$TMP/pack-render"
	mkdir -p "$RR"
	cp "$REPO_ROM" "$RR/rom.sms"
	(cd "$RR" && RETROPLUG_DETERMINISTIC=1 "$CLI" render rom.sms --duration 1s --out plain.wav >plain.out 2>plain.err)
	check "packs: a render with no pack exits 0" 0 $?
	(cd "$RR" && RETROPLUG_PACK_PATH="$PACKS/tsfix-render/1.0.0" RETROPLUG_DETERMINISTIC=1 \
		"$CLI" render rom.sms --duration 1s --out packed.wav >packed.out 2>packed.err)
	check "packs: a render with the render fixture pack exits 0" 0 $?
	cat "$RR/packed.out" "$RR/packed.err" >"$RR/packed.all"
	contains "packs: the render job's worker constructed the pack's role" "$RR/packed.all" "tsfix-render: constructed"
	if cmp -s "$RR/plain.wav" "$RR/packed.wav"; then
		echo "ok   - packs: the pack's role leaves the render's bytes as they were"
	else
		echo "FAIL - packs: the pack's role changed the render's bytes"
		fails=$((fails + 1))
	fi
	(cd "$RR" && RETROPLUG_PACK_PATH="$PACKS/tsfix-throws/1.0.0" RETROPLUG_DETERMINISTIC=1 \
		"$CLI" render rom.sms --duration 1s --out thrown.wav >thrown.out 2>thrown.err)
	check "packs: a render beside a pack whose half throws still exits 0" 0 $?
	cat "$RR/thrown.out" "$RR/thrown.err" >"$RR/thrown.all"
	contains "packs: the render job's console names the refused pack" "$RR/thrown.all" "pack tsfix-throws is refused"
else
	echo "skip - packs render leg: the smsggdj ROM is absent"
fi

# A render job's worker commits a pack's protocol data and placer, and its Engine plans and runs the pack's node
# (docs/downloadable-packs-m2e-plan.md): the fixture's placer says where its role's node was placed, nothing is refused,
# faulted or thrown, and the node writes nothing a core hears. mGB makes no sound without MIDI, so its WAV comparison
# only shows the pack adds nothing to a silent render; the smsggdj leg (in the ROM gate above) compares real audio.
# proto_render_leg <label> <dir> <rom file>: renders with and without the pack, then checks both.
proto_render_leg() {
	local label="$1" RP="$2" rom="$3"
	(cd "$RP" && RETROPLUG_DETERMINISTIC=1 "$CLI" render "$rom" --duration 1s --out plain.wav >plain.out 2>plain.err)
	check "packs: a render of $label with no pack exits 0" 0 $?
	(cd "$RP" && RETROPLUG_PACK_PATH="$PACKS/tsfix-proto/1.0.0" RETROPLUG_DETERMINISTIC=1 \
		"$CLI" render "$rom" --duration 1s --out proto.wav >proto.out 2>proto.err)
	check "packs: a render of $label with the protocol fixture pack exits 0" 0 $?
	cat "$RP/proto.out" "$RP/proto.err" >"$RP/proto.all"
	contains "packs: the $label render's placer saw the pack's node placed" "$RP/proto.all" \
		"tsfix-proto: placed rp.tsfix-proto.echo on system"
	if cmp -s "$RP/plain.wav" "$RP/proto.wav"; then
		echo "ok   - packs: the protocol fixture leaves the $label render's bytes as they were"
	else
		echo "FAIL - packs: the protocol fixture changed the $label render's bytes"
		fails=$((fails + 1))
	fi
	# A node that throws as it is created still renders and exits 0; it only logs.
	if grep -qE "refused|faulted|\[script\]" "$RP/proto.all"; then
		echo "FAIL - packs: the $label protocol render logged a fault: $(grep -E "refused|faulted|\[script\]" "$RP/proto.all" | head -1)"
		fails=$((fails + 1))
	else
		echo "ok   - packs: nothing was refused, faulted or thrown in the $label protocol render"
	fi
}
RP="$TMP/proto-render"
mkdir -p "$RP"
cp "$REPO/packages/mgb/rom/mGB.gb" "$RP/rom.gb"
proto_render_leg mGB "$RP" rom.gb
if [ -f "$REPO_ROM" ]; then
	RP="$TMP/proto-render-sms"
	mkdir -p "$RP"
	cp "$REPO_ROM" "$RP/rom.sms"
	proto_render_leg smsggdj "$RP" rom.sms
fi

echo
if [ "$fails" = "0" ]; then
	echo "PASS: retroplug-cli TypeScript harness"
else
	echo "FAIL: $fails check(s) failed"
fi
exit $((fails == 0 ? 0 : 1))
