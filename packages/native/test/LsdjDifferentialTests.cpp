// Differential oracle for the LSDJ sav codec. liblsdj is correct for song
// format versions <= 16, so for each content-bearing fixture sav we decode the
// working-memory song with BOTH liblsdj and our codec and assert the semantic
// fields match. This is what proves the old-format (`fmt < N`) decode branches
// are right — the byte-identical round-trip only proves losslessness.
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "lsdj/codec/SongCodec.hpp"

extern "C" {
#include <lsdj/instrument.h>
#include <lsdj/phrase.h>
#include <lsdj/sav.h>
#include <lsdj/song.h>
#include <lsdj/synth.h>
}

namespace fs = std::filesystem;
using namespace rp::lsdj;

namespace {

std::vector<std::uint8_t> slurp(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

const fs::path kDir{RETROPLUG_LSDJ_DIFF_SAV_DIR};

// Accumulate human-readable mismatches for one sav.
struct Diff {
    std::string sav;
    std::vector<std::string> bad;
    template <class A, class B>
    void eq(const char* what, A mine, B ref) {
        if (static_cast<long long>(mine) != static_cast<long long>(ref) && bad.size() < 12)
            bad.push_back(std::string(what) + " mine=" + std::to_string((long long)mine) +
                          " ref=" + std::to_string((long long)ref));
        else if (static_cast<long long>(mine) != static_cast<long long>(ref))
            ; // capped
    }
    bool ok() const { return bad.empty(); }
};

} // namespace

TEST_CASE("LSDJ song decode matches liblsdj across fmt<=16", "[lsdj-diff]") {
    if (!fs::exists(kDir)) { WARN("liblsdj fixture dir missing: " << kDir.string()); return; }

    std::size_t checked = 0;
    std::vector<std::string> failedSavs;

    for (const auto& entry : fs::directory_iterator(kDir)) {
        if (entry.path().extension() != ".sav") continue;
        const auto bytes = slurp(entry.path());
        if (bytes.size() < 0x8000) continue;

        // liblsdj reads the full sav image.
        lsdj_sav_t* lsav = nullptr;
        if (lsdj_sav_read_from_memory(bytes.data(), bytes.size(), &lsav, nullptr) != LSDJ_SUCCESS || !lsav)
            continue; // not a liblsdj-readable sav; skip
        const lsdj_song_t* L = lsdj_sav_get_working_memory_song_const(lsav);

        const int fmt = lsdj_song_get_format_version(L);
        if (fmt > 16) { lsdj_sav_free(lsav); continue; } // liblsdj authoritative only <=16

        auto res = codec::decodeSong(std::span<const std::uint8_t>(bytes.data(), 0x8000));
        REQUIRE(res);
        const model::Song& S = res.value();

        Diff d; d.sav = entry.path().filename().string() + " (fmt" + std::to_string(fmt) + ")";
        d.eq("formatVersion", S.formatVersion, fmt);
        d.eq("tempo", S.settings.tempo, lsdj_song_get_tempo(L));

        // Instruments.
        for (std::uint8_t i = 0; i < 0x40; ++i) {
            const bool lAlloc = lsdj_instrument_is_allocated(L, i);
            const bool mAlloc = S.instruments[i].has_value();
            if (lAlloc != mAlloc) { d.eq(("instr alloc#" + std::to_string(i)).c_str(), mAlloc, lAlloc); continue; }
            if (!lAlloc) continue;

            const int lType = static_cast<int>(lsdj_instrument_get_type(L, i));
            S.instruments[i]->visit([&](const auto& v) {
                using T = std::decay_t<decltype(v)>;
                const std::string pre = "instr#" + std::to_string(i) + ".";
                d.eq((pre + "panning").c_str(),
                     static_cast<int>(v.common.get().panning),
                     static_cast<int>(lsdj_instrument_get_panning(L, i)));
                // Vibrato (pulse/wave/noise) — exercises the fmt<4 cross-coding.
                if constexpr (!std::is_same_v<T, model::KitInstrument>) {
                    d.eq((pre + "vibDir").c_str(), static_cast<int>(v.vibrato.direction),
                         static_cast<int>(lsdj_instrument_get_vibrato_direction(L, i)));
                    d.eq((pre + "vibShape").c_str(), static_cast<int>(v.vibrato.shape),
                         static_cast<int>(lsdj_instrument_get_vibrato_shape(L, i)));
                    d.eq((pre + "plv").c_str(), static_cast<int>(v.vibrato.plvSpeed),
                         static_cast<int>(lsdj_instrument_get_plv_speed(L, i)));
                }
                if constexpr (std::is_same_v<T, model::PulseInstrument>) {
                    d.eq((pre + "type").c_str(), 0, lType);
                    d.eq((pre + "pulseWidth").c_str(), static_cast<int>(v.pulseWidth),
                         static_cast<int>(lsdj_instrument_pulse_get_pulse_width(L, i)));
                    d.eq((pre + "finetune").c_str(), v.finetune.value(), lsdj_instrument_pulse_get_finetune(L, i));
                    d.eq((pre + "sweep").c_str(), v.sweep, lsdj_instrument_pulse_get_sweep(L, i));
                } else if constexpr (std::is_same_v<T, model::WaveInstrument>) {
                    d.eq((pre + "type").c_str(), 1, lType);
                    d.eq((pre + "synth").c_str(), v.synth.value(), lsdj_instrument_wave_get_synth(L, i));
                    d.eq((pre + "playMode").c_str(), static_cast<int>(v.playMode),
                         static_cast<int>(lsdj_instrument_wave_get_play_mode(L, i)));
                    d.eq((pre + "length").c_str(), v.length.value(), lsdj_instrument_wave_get_length(L, i));
                    d.eq((pre + "speed").c_str(), v.speed, lsdj_instrument_wave_get_speed(L, i));
                    d.eq((pre + "loopPos").c_str(), v.loopPos.value(), lsdj_instrument_wave_get_loop_pos(L, i));
                } else if constexpr (std::is_same_v<T, model::KitInstrument>) {
                    d.eq((pre + "type").c_str(), 2, lType);
                    d.eq((pre + "kit1").c_str(), v.kit1.value(), lsdj_instrument_kit_get_kit1(L, i));
                    d.eq((pre + "kit2").c_str(), v.kit2.value(), lsdj_instrument_kit_get_kit2(L, i));
                    d.eq((pre + "distortion").c_str(), static_cast<int>(v.distortion),
                         static_cast<int>(lsdj_instrument_kit_get_distortion_mode(L, i)));
                    d.eq((pre + "offset1").c_str(), v.offset1, lsdj_instrument_kit_get_offset1(L, i));
                } else if constexpr (std::is_same_v<T, model::NoiseInstrument>) {
                    d.eq((pre + "type").c_str(), 3, lType);
                    d.eq((pre + "shape").c_str(), v.shape, lsdj_instrument_noise_get_shape(L, i));
                    d.eq((pre + "stability").c_str(), static_cast<int>(v.stability),
                         static_cast<int>(lsdj_instrument_noise_get_stability(L, i)));
                }
            });
        }

        // Phrases (exercises the fmt<8 command remap heavily).
        for (std::uint16_t p = 0; p < 0xFF; ++p) {
            if (!lsdj_phrase_is_allocated(L, static_cast<std::uint8_t>(p))) continue;
            if (!S.phrases[p]) { d.eq(("phrase alloc#" + std::to_string(p)).c_str(), 0, 1); continue; }
            const auto& mp = *S.phrases[p];
            for (std::uint8_t s = 0; s < 16; ++s) {
                const std::string pre = "ph#" + std::to_string(p) + "." + std::to_string(s) + ".";
                d.eq((pre + "note").c_str(), mp.notes[s], lsdj_phrase_get_note(L, p, s));
                const int mInstr = mp.instruments[s] ? *mp.instruments[s] : 0xFF;
                d.eq((pre + "instr").c_str(), mInstr, lsdj_phrase_get_instrument(L, p, s));
                d.eq((pre + "cmd").c_str(), static_cast<int>(mp.commands[s]),
                     static_cast<int>(lsdj_phrase_get_command(L, p, s)));
                d.eq((pre + "cmdval").c_str(), mp.commandValues[s], lsdj_phrase_get_command_value(L, p, s));
            }
        }

        // Synths (exercises the fmt<5 resonance branch).
        for (std::uint8_t s = 0; s < 0x10; ++s) {
            const std::string pre = "synth#" + std::to_string(s) + ".";
            d.eq((pre + "waveform").c_str(), static_cast<int>(S.synths[s].waveform),
                 static_cast<int>(lsdj_synth_get_waveform(L, s)));
            d.eq((pre + "filter").c_str(), static_cast<int>(S.synths[s].filter),
                 static_cast<int>(lsdj_synth_get_filter(L, s)));
            d.eq((pre + "resStart").c_str(), S.synths[s].resonanceStart.value(), lsdj_synth_get_resonance_start(L, s));
            d.eq((pre + "resEnd").c_str(), S.synths[s].resonanceEnd.value(), lsdj_synth_get_resonance_end(L, s));
        }

        lsdj_sav_free(lsav);
        ++checked;
        if (!d.ok()) {
            std::string msg = d.sav + ":";
            for (const auto& b : d.bad) msg += "\n    " + b;
            UNSCOPED_INFO(msg);
            failedSavs.push_back(d.sav);
        }
    }

    INFO("checked " << checked << " fmt<=16 savs; " << failedSavs.size() << " with mismatches");
    CHECK(checked >= 8);              // the fixture corpus is present
    CHECK(failedSavs.empty());
}
