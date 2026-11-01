// The kit compilers' byte baseline: every kit these compilers build is recorded as a bank hash, so a change that
// moves one byte of one kit fails here.
//
// Every case synthesises its source audio in this file, writes it to a temp dir as a 32-bit float WAV, and compiles
// it through the same objects the `compileKit` / `compileDmc` RPCs build - `rp::kit::KitCompiler` over
// `LsdjKitCodec` / `RisaDmcCodec`, with the source decoded from its path by `SampleCache` - so the baseline covers
// the real decode path (miniaudio) as well as the window, the effects, the r8brain resample and the pack. Nothing
// here reads a fixture: the matrix runs wherever the binary does.
//
// The matrix is fixed. Sources: two sine frequencies, a chirp, seeded noise (`std::mt19937`, a fixed seed) and
// silence, each at 22.05, 44.1 and 48 kHz, plus a 600-frame source shorter than one resampler chunk. LSDj
// cases: no effect, a non-unity gain with normalise off, the same gain with normalise on, that normalise over a
// silent source, each of the eight filter types, each of the five dither algorithms, rotate on and off, one chain of
// filter then gain then dither, the short source on its own, and one bank carrying a second slot beside the first.
// risa cases: the DMC codec at five rate indices, with and without a pre-effect, with normalise off and with loop set,
// a normalise of a silent source, the short source on its own, and one bank of two slots. One of the two-slot cases
// names its bank with a lowercase, over-long string, so `writeString`'s uppercase and truncation are in the baseline
// as well as the assembler's offset accumulation for every slot but the first.
//
// Exact bytes are compared only on the host the golden was recorded on (`linux-x64-gnu13`), as the engine's parity
// baseline does, because float arithmetic is bit-stable only for one compiler on one architecture. Elsewhere a case
// compiles twice and must agree with itself, and its bank size and slot length are still held to the golden.
// RP_KIT_GOLDEN_REQUIRE_TARGET=1 makes a foreign host fail instead of falling back, as RP_PARITY_REQUIRE_TARGET does
// for parity (CI's linux-x64 job sets both, so neither exact check can fade out on the host that has it).
//
// RP_KIT_GOLDEN_UPDATE=1 re-records golden/kit-baseline.json from this tree, and only on the baseline's target: a
// recording refuses a case whose two compiles disagree, because such a case depends on something outside its input
// and its digest would mean nothing, and it refuses to write a file anywhere else, so a fallback digest can never be
// recorded. Without the variable the file is only ever read.
//
// Run via `pnpm test:plugin`, or one tag: `build/bin/retroplug-audio-test "[kit-baseline]"`.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <rfl/json.hpp>

#include "dsp/Resample.hpp"  // rp::dsp::Resampler::kChunk, the chunk size the short source is shorter than
#include "kit/KitCompiler.hpp"
#include "lsdj/Effects.hpp"
#include "lsdj/LsdjKitCodec.hpp"
#include "lsdj/KitUtil.hpp"  // rp::lsdj::Kit, the LSDj bank layout
#include "risa/RisaDmcCodec.hpp"
#include "util/BuildTarget.hpp"
#include "util/Hash.hpp"

// A golden case's record: the bank's FNV-1a-64 in lowercase hex, the bank's size, and slot 0's length. The size and
// the slot length are compared on every host (a bank layout is not float arithmetic); the hash only on the target it
// was recorded on. The slot length is the half that carries information off the target: a bank is a fixed size
// whatever happened, but a slot is as long as the resample and the pack made it, so a truncated resample on a foreign
// host moves it. It is as long as the bank says, which quantises differently per codec: LSDj's offset table gives the
// slot's exact packed bytes (2 per byte, so the last nibble pair padded), while risa's index entry holds a length
// register whose (reg + 1) * 16 is the $4013 playback span, up to 15 bytes past what was written.
//
// These structs live in a NAMED namespace deliberately: reflect-cpp reflects a struct through a fake-object
// conversion operator, which clang refuses to define for a type with no linkage.
namespace kit_baseline {

struct BankGolden {
    std::string   hash;
    std::uint64_t size = 0;
    std::uint64_t slot = 0;
};

struct Golden {
    std::string                       target;
    std::map<std::string, BankGolden> cases;
};

} // namespace kit_baseline

namespace {

/** The host name the parity baseline's `PARITY_TARGET` uses, from `rp::buildTarget()` exactly as the engine's
 *  `buildTarget` RPC reports it, so a baseline recorded here and one recorded there name one toolchain. */
constexpr const char* kBaselineTarget = "linux-x64-gnu13";

bool envFlag(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && std::string_view(value) == "1";
}

/** The source signals. Every one is used at every rate below (the short one only at 44.1 kHz, a rate whose resampler
 *  chunk it is shorter than), so a case that changed only one rate's source would not go untested. Silence is a
 *  source of its own because it is the only input that reaches the effects' "no peak to normalise by" branch. */
enum class Signal { Sine440, Sine1200, Chirp, Noise, Silence, Short };

struct SignalDesc {
    Signal      signal;
    const char* name;
    std::size_t frames;
};

constexpr SignalDesc kSignals[] = {
    {Signal::Sine440, "sine440", 8192},
    {Signal::Sine1200, "sine1200", 8192},
    {Signal::Chirp, "chirp", 8192},
    {Signal::Noise, "noise", 8192},
    {Signal::Silence, "silence", 8192},
    {Signal::Short, "short", 600},
};

constexpr std::uint32_t kRates[] = {22050, 44100, 48000};

constexpr double kPi = 3.14159265358979323846;

constexpr std::size_t signalFrames(Signal signal) {
    for (const SignalDesc& desc : kSignals) {
        if (desc.signal == signal) return desc.frames;
    }
    return 0;
}

std::vector<float> sine(double hz, std::uint32_t rate, std::size_t frames) {
    std::vector<float> out(frames);
    for (std::size_t i = 0; i < frames; ++i) {
        const double t = static_cast<double>(i) / static_cast<double>(rate);
        out[i] = 0.8f * static_cast<float>(std::sin(2.0 * kPi * hz * t));
    }
    return out;
}

/** A linear sweep from 200 Hz to 4000 Hz across the buffer, its phase integrated so the sweep stays continuous. */
std::vector<float> chirp(std::uint32_t rate, std::size_t frames) {
    std::vector<float> out(frames);
    double phase = 0.0;
    for (std::size_t i = 0; i < frames; ++i) {
        const double f = 200.0 + (4000.0 - 200.0) * static_cast<double>(i) / static_cast<double>(frames);
        phase += 2.0 * kPi * f / static_cast<double>(rate);
        out[i] = 0.7f * static_cast<float>(std::sin(phase));
    }
    return out;
}

/** Uniform noise in [-0.6, 0.6) from raw `mt19937` output: the generator's sequence is specified and the draw is exact
 *  in binary32, so this source is the same on every host (a `uniform_real_distribution`'s is not specified). */
std::vector<float> noise(std::size_t frames) {
    std::mt19937 rng(0x5eed1234u);
    std::vector<float> out(frames);
    for (float& v : out) {
        const float unit = static_cast<float>(rng() >> 8) * (1.0f / 16777216.0f);
        v = 1.2f * unit - 0.6f;
    }
    return out;
}

std::vector<float> signalSamples(Signal signal, std::uint32_t rate) {
    const std::size_t frames = signalFrames(signal);
    switch (signal) {
    case Signal::Sine440:  return sine(440.0, rate, frames);
    case Signal::Sine1200: return sine(1200.0, rate, frames);
    case Signal::Chirp:    return chirp(rate, frames);
    case Signal::Noise:    return noise(frames);
    case Signal::Silence:  return std::vector<float>(frames, 0.0f);
    case Signal::Short:    return sine(440.0, rate, frames);
    }
    return {};
}

const char* signalName(Signal signal) {
    for (const SignalDesc& desc : kSignals) {
        if (desc.signal == signal) return desc.name;
    }
    return "";
}

/**
 * A 32-bit float, mono WAV (fmt tag 3). Float rather than 16-bit PCM because the sources are synthesised in float and
 * miniaudio decodes a float WAV straight into `ma_format_f32`, so the samples the compilers see are exactly the ones
 * this test computed: a 16-bit source would quantise them before the compilers ever ran, and the golden's job is to
 * pin the DSP on known values. Every field is written byte by byte, little-endian, so a big-endian host writes the
 * same file.
 */
void writeFloatWav(const std::filesystem::path& path, const std::vector<float>& samples, std::uint32_t rate) {
    std::vector<std::uint8_t> bytes;
    const auto u16 = [&](std::uint16_t v) {
        bytes.push_back(static_cast<std::uint8_t>(v));
        bytes.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    const auto u32 = [&](std::uint32_t v) {
        for (int shift = 0; shift < 32; shift += 8) bytes.push_back(static_cast<std::uint8_t>(v >> shift));
    };
    const auto tag = [&](std::string_view s) { bytes.insert(bytes.end(), s.begin(), s.end()); };

    const std::uint32_t dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));
    tag("RIFF");
    u32(36u + dataBytes);
    tag("WAVE");
    tag("fmt ");
    u32(16);               // fmt chunk size
    u16(3);                // IEEE float
    u16(1);                // mono
    u32(rate);
    u32(rate * 4);         // byte rate
    u16(4);                // block align
    u16(32);               // bits per sample
    tag("data");
    u32(dataBytes);
    for (const float v : samples) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof bits);
        u32(bits);
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    REQUIRE(out.good()); // a failed write would otherwise surface as "cannot decode" from the cache below
}

enum class KitCodec { Lsdj, Risa };

/** A sample after slot 0: its name and its own effects, compiled from the case's source, rate and flags. */
struct ExtraSlot {
    const char*                       name;
    std::vector<rp::lsdj::LsdjEffect> effects;
};

/** One matrix entry. `rate` is the source's own rate; `dmcRate` is risa's PAL DPCM rate index, unused by LSDj;
 *  `normalize` and `loop` are risa's DMC encode flags, unused by LSDj (LSDj normalises through a `GainEffect`).
 *  `extraSlots` adds samples after slot 0; `kitName` overrides the codec's own bank name. */
struct KitCase {
    const char*                       name;
    KitCodec                          codec;
    Signal                            signal;
    std::uint32_t                     rate;
    std::vector<rp::lsdj::LsdjEffect> effects;
    bool                              rotate    = true;
    std::uint8_t                      dmcRate   = 12;
    bool                              normalize = true;
    bool                              loop      = false;
    std::vector<ExtraSlot>            extraSlots;
    const char*                       kitName = nullptr;
};

rp::lsdj::LsdjEffect gainEffect(float gain, bool normalize) {
    return rp::lsdj::GainEffect{normalize, gain};
}

rp::lsdj::LsdjEffect filterEffect(rp::lsdj::FilterType type, float frequency, float q, float gainDb = 0.0f) {
    return rp::lsdj::FilterEffect{type, frequency, q, gainDb};
}

rp::lsdj::LsdjEffect ditherEffect(rp::lsdj::DitherType type) {
    return rp::lsdj::DitherEffect{type};
}

/** The matrix, in the order this file lists it (the golden's map sorts by name). Filters run at the source rate,
 *  before the resample, so their frequencies are the source's; every one is below 11.025 kHz, valid at all rates. */
std::vector<KitCase> kitCases() {
    using rp::lsdj::DitherType;
    using rp::lsdj::FilterType;

    return {
        {"lsdj/no-effect", KitCodec::Lsdj, Signal::Sine440, 44100, {}},
        {"lsdj/gain", KitCodec::Lsdj, Signal::Sine440, 44100, {gainEffect(0.37f, false)}},
        {"lsdj/normalize", KitCodec::Lsdj, Signal::Sine440, 44100, {gainEffect(0.37f, true)}},
        // A silent source is the only input that reaches `GainEffect`'s "maxAmp > 0" guard, the one division in the
        // effect: a non-silent source never takes that branch.
        {"lsdj/normalize-silence", KitCodec::Lsdj, Signal::Silence, 44100, {gainEffect(0.37f, true)}},
        {"lsdj/filter/low-pass", KitCodec::Lsdj, Signal::Sine440, 44100,
         {filterEffect(FilterType::LowPass, 2000.0f, 0.9f)}},
        {"lsdj/filter/high-pass", KitCodec::Lsdj, Signal::Sine440, 22050,
         {filterEffect(FilterType::HighPass, 800.0f, 1.4f)}},
        {"lsdj/filter/band-pass", KitCodec::Lsdj, Signal::Sine1200, 48000,
         {filterEffect(FilterType::BandPass, 1200.0f, 2.0f)}},
        {"lsdj/filter/band-stop", KitCodec::Lsdj, Signal::Chirp, 48000,
         {filterEffect(FilterType::BandStop, 4000.0f, 0.7f)}},
        {"lsdj/filter/peak", KitCodec::Lsdj, Signal::Sine1200, 44100,
         {filterEffect(FilterType::Peak, 1000.0f, 1.0f, 6.0f)}},
        {"lsdj/filter/low-shelf", KitCodec::Lsdj, Signal::Noise, 22050,
         {filterEffect(FilterType::LowShelf, 800.0f, 0.7f, -6.0f)}},
        {"lsdj/filter/high-shelf", KitCodec::Lsdj, Signal::Chirp, 22050,
         {filterEffect(FilterType::HighShelf, 3000.0f, 0.7f, 4.0f)}},
        {"lsdj/filter/all-pass", KitCodec::Lsdj, Signal::Noise, 48000,
         {filterEffect(FilterType::AllPass, 1500.0f, 1.2f)}},
        {"lsdj/dither/high-pass-tpdf", KitCodec::Lsdj, Signal::Sine1200, 22050,
         {ditherEffect(DitherType::HighPassTPDF)}},
        {"lsdj/dither/shaped-tpdf", KitCodec::Lsdj, Signal::Noise, 44100,
         {ditherEffect(DitherType::ShapedTPDF)}},
        {"lsdj/dither/error-diffusion", KitCodec::Lsdj, Signal::Chirp, 44100,
         {ditherEffect(DitherType::ErrorDiffusion)}},
        {"lsdj/dither/jjn", KitCodec::Lsdj, Signal::Sine440, 48000,
         {ditherEffect(DitherType::JJN)}},
        {"lsdj/dither/sierra-lite", KitCodec::Lsdj, Signal::Sine1200, 44100,
         {ditherEffect(DitherType::SierraLite)}},
        // The pair differs in `rotate` alone, so a rotate that stopped packing (or stopped round-tripping) moves one
        // of the two banks and not the other.
        {"lsdj/rotate-off", KitCodec::Lsdj, Signal::Chirp, 44100, {}, false},
        {"lsdj/rotate-on", KitCodec::Lsdj, Signal::Chirp, 44100, {}, true},
        {"lsdj/chain/filter-gain-dither", KitCodec::Lsdj, Signal::Noise, 22050,
         {filterEffect(FilterType::LowPass, 3000.0f, 1.0f), gainEffect(0.5f, false),
          ditherEffect(DitherType::HighPassTPDF)}},
        {"lsdj/short-source", KitCodec::Lsdj, Signal::Short, 44100, {}},
        // Two slots, the second named SNR and shaped by a gain slot 0 does not have: the assembler's offset
        // accumulation (offsets[i + 1] for i > 0), its 15-slot loop and the sample-name rule are inside the baseline,
        // and so is `writeString`, on a lowercase over-long bank name it has to uppercase and truncate.
        {"lsdj/two-slots", KitCodec::Lsdj, Signal::Sine440, 44100, {}, true, 12, true, false,
         {{"SNR", {gainEffect(0.5f, false)}}}, "baseline-kit"},

        {"risa/no-effect-rate-4", KitCodec::Risa, Signal::Sine440, 44100, {}, true, 4},
        {"risa/no-effect-rate-12", KitCodec::Risa, Signal::Chirp, 48000, {}, true, 12},
        {"risa/no-effect-rate-15", KitCodec::Risa, Signal::Noise, 22050, {}, true, 15},
        {"risa/gain-rate-10", KitCodec::Risa, Signal::Sine1200, 44100, {gainEffect(0.4f, false)}, true, 10},
        {"risa/filter-rate-8", KitCodec::Risa, Signal::Noise, 48000,
         {filterEffect(FilterType::LowPass, 3000.0f, 1.0f)}, true, 8},
        {"risa/short-source-rate-12", KitCodec::Risa, Signal::Short, 44100, {}, true, 12},
        // The DMC encode's other two branches, reachable from the RPC (`normalize.value_or(true)`,
        // `loop.value_or(false)`): the non-normalising 7-bit map and the loop flag in the index entry's byte 3.
        {"risa/no-normalize-rate-12", KitCodec::Risa, Signal::Noise, 44100, {}, true, 12, false},
        {"risa/loop-rate-4", KitCodec::Risa, Signal::Sine440, 44100, {}, true, 4, true, true},
        // A silent source pins `convertF32ToDpcm`'s peak floor of 1 at both remaining rates, and with it the
        // no-peak branch of the DMC normalise.
        {"risa/normalize-silence-rate-12", KitCodec::Risa, Signal::Silence, 22050, {}, true, 12},
        {"risa/normalize-silence-rate-4", KitCodec::Risa, Signal::Silence, 48000, {}, true, 4},
        // The two-slot twin on risa's side: its index entries and the sample-data cursor move for slot 1, and the
        // second slot's rate and effects are its own.
        {"risa/two-slots-rate-12", KitCodec::Risa, Signal::Chirp, 48000, {}, true, 12, true, false,
         {{"SNR", {gainEffect(0.4f, false)}}}},
    };
}

/** One source file, by signal and rate, as it was written and as it should decode. */
struct SourceFile {
    std::filesystem::path    path;
    std::uint32_t            rate = 0;
    std::vector<float>       samples;
};

std::string sourceKey(Signal signal, std::uint32_t rate) {
    return std::string(signalName(signal)) + "-" + std::to_string(rate);
}

/** A directory of its own for the run's WAVs, removed on the way out (including on a failed assertion). The name is
 *  drawn from `std::random_device` so two runs sharing a temp dir cannot write over each other's sources. */
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        std::mt19937 rng(std::random_device{}());
        path = std::filesystem::temp_directory_path() / ("rp-kit-baseline-" + std::to_string(rng()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
    TempDir(const TempDir&)            = delete;
    TempDir& operator=(const TempDir&) = delete;
};

/** Compile one case the way its RPC does: build the codec with its slots, then hand it to a shared compiler. The
 *  compiler owns the `SampleCache`, so a case compiled twice decodes once; `SampleCache` keys on the path and both of
 *  a case's compiles pass the same one. */
rp::kit::CompiledKit compileCase(rp::kit::KitCompiler& compiler, const KitCase& c,
                                 const SourceFile& source) {
    const char* kitName = c.kitName != nullptr ? c.kitName : (c.codec == KitCodec::Lsdj ? "BASELINE" : "RISA");
    if (c.codec == KitCodec::Lsdj) {
        rp::lsdj::CompileSampleSpec slot;
        slot.path    = source.path.string();
        slot.name    = "KIK";
        slot.effects = c.effects;
        slot.rotate  = c.rotate;
        std::vector<rp::lsdj::CompileSampleSpec> slots{slot};
        for (const ExtraSlot& extra : c.extraSlots) {
            rp::lsdj::CompileSampleSpec next = slot;
            next.name    = extra.name;
            next.effects = extra.effects;
            slots.push_back(std::move(next));
        }
        const rp::lsdj::LsdjKitCodec codec(kitName, std::move(slots));
        return compiler.compile(codec);
    }

    rp::risa::CompileDmcSampleSpec slot;
    slot.path      = source.path.string();
    slot.name      = "KIK";
    slot.effects   = c.effects;
    slot.rate      = c.dmcRate;
    slot.normalize = c.normalize;
    slot.loop      = c.loop;
    std::vector<rp::risa::CompileDmcSampleSpec> slots{slot};
    for (const ExtraSlot& extra : c.extraSlots) {
        rp::risa::CompileDmcSampleSpec next = slot;
        next.name    = extra.name;
        next.effects = extra.effects;
        slots.push_back(std::move(next));
    }
    const rp::risa::RisaDmcCodec codec(kitName, std::move(slots));
    return compiler.compile(codec);
}

/** Whether the bank carries every sample the case compiles. A case whose decode or resample failed leaves that slot
 *  empty, and an empty slot is a stable bank, so without this a broken case would pin nothing and still pass. */
bool bankHasSlots(const KitCase& c, const std::vector<std::uint8_t>& bank) {
    const std::size_t slots = 1 + c.extraSlots.size();
    if (c.codec == KitCodec::Lsdj) {
        // The offset table's little-endian u16 entries: offsets[0] is the sample-data start, offsets[i + 1] the end of
        // slot i. A slot with no sample gets neither, and the table only moves forward, so slot i is present exactly
        // when entry(i + 1) is past entry(i), which is also the accumulation a second slot reaches.
        if (bank.size() != rp::lsdj::Kit::kSize) return false;
        const auto entry = [&](std::size_t i) {
            return static_cast<std::uint16_t>(bank[i * 2] | (bank[i * 2 + 1] << 8));
        };
        if (entry(0) != rp::lsdj::Kit::kBankBase + rp::lsdj::Kit::kSampleDataOffset) return false;
        for (std::size_t i = 0; i < slots; ++i) {
            if (entry(i + 1) <= entry(i)) return false;
        }
        return true;
    }
    // The risa bank's index entries: a slot's address byte is 0xFF when the slot is empty.
    using rp::risa::RisaDmcCodec;
    if (bank.size() != RisaDmcCodec::kBankSize) return false;
    for (std::size_t i = 0; i < slots; ++i) {
        if (bank[RisaDmcCodec::kIndexOffset + i * RisaDmcCodec::kIndexEntry] == 0xFF) return false;
    }
    return true;
}

/** Slot 0's length in an assembled bank, as the bank itself records it: LSDj's offset table gives the start and end of
 *  the slot's packed nibbles (an exact byte count), and risa's length register is the index entry's `(reg + 1) * 16`,
 *  the $4013 playback span, which covers the bytes written plus up to 15 of padding. Unlike the bank size this is not
 *  a constant: it follows the resample and the pack. */
std::uint64_t slotLength(const KitCase& c, const std::vector<std::uint8_t>& bank) {
    if (c.codec == KitCodec::Lsdj) {
        const auto entry = [&](std::size_t i) {
            return static_cast<std::uint16_t>(bank[i * 2] | (bank[i * 2 + 1] << 8));
        };
        return static_cast<std::uint64_t>(entry(1) - entry(0));
    }
    using rp::risa::RisaDmcCodec;
    const std::uint8_t lengthReg = bank[RisaDmcCodec::kIndexOffset + 1];
    return static_cast<std::uint64_t>(lengthReg + 1) * 16;
}

std::string hexHash(std::uint64_t hash) {
    char buffer[17];
    std::snprintf(buffer, sizeof buffer, "%016llx", static_cast<unsigned long long>(hash));
    return buffer;
}

/**
 * Writes the golden through a sibling temp file renamed into place, so a failed record leaves the committed file as it
 * was and says so: truncating the golden in place would destroy it on a read-only directory or a full disk, and a
 * rename within one directory is atomic, so a reader never sees a half-written golden either.
 */
void writeGolden(const std::filesystem::path& path, const kit_baseline::Golden& golden) {
    std::filesystem::create_directories(path.parent_path());
    const std::filesystem::path temp = path.string() + ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        out << "{\n  \"target\": \"" << golden.target << "\",\n  \"cases\": {\n";
        std::size_t written = 0;
        for (const auto& [name, bank] : golden.cases) {
            out << "    \"" << name << "\": { \"hash\": \"" << bank.hash << "\", \"size\": " << bank.size
                << ", \"slot\": " << bank.slot << " }";
            out << (++written < golden.cases.size() ? ",\n" : "\n");
        }
        out << "  }\n}\n";
        out.close();
        if (!out.good()) {
            std::error_code ignored;
            std::filesystem::remove(temp, ignored);
            FAIL("cannot write " << temp.string() << ", so the recorded golden is left as it was");
        }
    }
    std::error_code ec;
    std::filesystem::rename(temp, path, ec);
    if (ec) {
        std::error_code ignored;
        std::filesystem::remove(temp, ignored);
        FAIL("cannot rename " << temp.string() << " onto " << path.string() << ": " << ec.message());
    }
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string text;
    if (!in) return text;
    in.seekg(0, std::ios::end);
    text.resize(static_cast<std::size_t>(in.tellg()));
    in.seekg(0, std::ios::beg);
    in.read(text.data(), static_cast<std::streamsize>(text.size()));
    return text;
}

} // namespace

TEST_CASE("every kit case compiles to the bank the golden records", "[kit-baseline][kit]") {
    const auto cases = kitCases();
    const std::filesystem::path goldenPath = std::filesystem::path(RP_KIT_GOLDEN_DIR) / "kit-baseline.json";
    const std::string host = rp::buildTarget();
    const bool onTarget = host == kBaselineTarget;
    const bool update = envFlag("RP_KIT_GOLDEN_UPDATE");
    const bool requireTarget = envFlag("RP_KIT_GOLDEN_REQUIRE_TARGET");

    static_assert(signalFrames(Signal::Short) < rp::dsp::Resampler::kChunk,
                  "the short source must be shorter than one resampler chunk");

    TempDir dir;

    // Every (signal, rate) pair a case names is written once, and every signal is used at every rate: the matrix's
    // claim about its own coverage, checked rather than trusted.
    std::map<std::string, SourceFile> sources;
    for (const KitCase& c : cases) {
        const std::string key = sourceKey(c.signal, c.rate);
        if (sources.find(key) != sources.end()) continue;
        SourceFile source;
        source.rate    = c.rate;
        source.samples = signalSamples(c.signal, c.rate);
        source.path    = dir.path / (key + ".wav");
        writeFloatWav(source.path, source.samples, source.rate);
        sources.emplace(key, std::move(source));
    }
    for (const SignalDesc& desc : kSignals) {
        for (const std::uint32_t rate : kRates) {
            if (desc.signal == Signal::Short && rate != 44100) continue;
            INFO("signal " << desc.name << " at " << rate << " Hz");
            CHECK(sources.count(sourceKey(desc.signal, rate)) == 1);
        }
    }

    // The decode step is part of what the baseline covers, so hold it to the files written above: the same rate, the
    // same frame count, and the same samples. The cache decodes each path once, and every case's compile below reuses
    // this entry, so a WAV miniaudio read differently would show up here before a bank hash is even reached.
    rp::kit::KitCompiler compiler;
    for (const auto& [key, source] : sources) {
        INFO("source " << key);
        const rp::kit::SampleData* data = compiler.cache().getOrLoad(source.path.string());
        REQUIRE(data != nullptr);
        CHECK(data->sampleRate == source.rate);
        CHECK(data->buffer.size() == source.samples.size());
        CHECK(data->buffer == source.samples);
    }

    if (update) {
        // A digest recorded on any other host would be a self-agreement fallback wearing the golden's clothes, so a
        // recording refuses to happen off the target rather than merely warning.
        if (!onTarget) {
            FAIL("a recording only happens on " << kBaselineTarget << ", and this host is " << host
                                               << "; a digest recorded here would be a fallback, not the baseline");
        }
        kit_baseline::Golden recorded{kBaselineTarget, {}};
        for (const KitCase& c : cases) {
            INFO("case " << c.name);
            const auto first = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            REQUIRE(first.ok);
            REQUIRE(first.error.empty());
            REQUIRE(bankHasSlots(c, first.bytes));
            const auto second = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            // REQUIRE, not CHECK: a case whose two compiles disagree depends on something outside its input, so its
            // digest would mean nothing and writing it (let alone re-recording a different one next time) would put
            // a random bank in the commit. Aborting here leaves the file untouched.
            REQUIRE(second.bytes == first.bytes);
            recorded.cases[c.name] = {hexHash(first.hash), first.bytes.size(), slotLength(c, first.bytes)};
        }
        // The record is a map keyed by the case's name, so two cases sharing one name would collapse into one entry
        // and the matrix would still call the recording a success. Refuse a matrix the file cannot represent.
        REQUIRE(recorded.cases.size() == cases.size());
        writeGolden(goldenPath, recorded);
        SUCCEED("recorded " << recorded.cases.size() << " banks to " << goldenPath.string());
        return;
    }

    const std::string text = readFile(goldenPath);
    const auto parsed = rfl::json::read<kit_baseline::Golden>(text);
    if (!parsed.has_value()) {
        FAIL("cannot read " << goldenPath.string() << " (" << parsed.error().what()
                            << "); record it with RP_KIT_GOLDEN_UPDATE=1 on " << kBaselineTarget);
    }
    const kit_baseline::Golden golden = *parsed;
    // REQUIRE, not CHECK: a half-moved target (the constant moved, the golden not re-recorded) would otherwise print
    // one accurate line and then one hash mismatch per case, each blaming a case for the target.
    REQUIRE(golden.target == kBaselineTarget);
    // A renamed or dropped case must be re-recorded, not silently skipped: the golden holds exactly this matrix.
    REQUIRE(golden.cases.size() == cases.size());

    if (requireTarget && !onTarget) {
        FAIL("this host is " << host << " but the baseline was recorded on " << golden.target
                             << ", so exact kit bytes would not be compared; run on " << golden.target
                             << " (or re-record the baseline there and move kBaselineTarget)");
    }

    for (const KitCase& c : cases) {
        INFO("case " << c.name << " on " << host);
        const auto kit = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
        // These three hold `KitCompiler`'s contract, not the case's: `compile` sets `ok` unconditionally and computes
        // its hash as the FNV of the bytes it returns (the dirty-tracking hash the RPC throws away), so they fail
        // only if that contract changes, and the check keeps the golden a hash of the bank and nothing else.
        // `bankHasSlots` below is the case's own vacuous-pass guard.
        REQUIRE(kit.ok);
        REQUIRE(kit.error.empty());
        CHECK(kit.hash == rp::hash::fnv1a64(kit.bytes.data(), kit.bytes.size()));
        REQUIRE(bankHasSlots(c, kit.bytes));

        const auto expected = golden.cases.find(c.name);
        REQUIRE(expected != golden.cases.end());
        CHECK(expected->second.size == kit.bytes.size());
        CHECK(expected->second.slot == slotLength(c, kit.bytes));
        if (onTarget) {
            CHECK(expected->second.hash == hexHash(kit.hash));
        } else {
            // Off the target only self-agreement is meaningful; the recorded digest is not comparable, but the case
            // still has to be reproducible from its input alone.
            const auto again = compileCase(compiler, c, sources.at(sourceKey(c.signal, c.rate)));
            CHECK(again.hash == kit.hash);
            CHECK(again.bytes == kit.bytes);
        }
    }
}
