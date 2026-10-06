// Guards MidiIo's input ring against its real threading: MANY producers, one consumer. Every open RtMidiIn
// (the always-open virtual "RetroPlug In" plus one per selected hardware input) delivers on its own
// callback thread, so picking a hardware device - or All Devices - puts two or more threads in pushRing at
// once, while the audio thread drains with poll(). The ring was written for a single producer, which let
// those threads race the tail index, the sequence counter and a slot's vector.
//
// injectForTest() is the same pushRing an RtMidi callback calls, and neither it nor poll() touches RtMidi,
// so this needs no MIDI system. A plain build only catches the race when it happens to tear a slot; under
// ThreadSanitizer (build this target in build-tsan/) an unserialized producer side is reported every run.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#include "host/input/MidiIo.hpp"

namespace {

constexpr int kProducers = 4;
constexpr int kPerProducer = 5000;

// Message i from producer p: a status byte naming the producer, i split over two data bytes, then a tail
// whose length (0-4 bytes) and contents both depend on i - so a slot assembled from two writers fails the
// decode rather than passing as some other valid message.
std::vector<std::uint8_t> encode(int p, int i) {
    std::vector<std::uint8_t> m = {static_cast<std::uint8_t>(0x90 | p), static_cast<std::uint8_t>(i & 0x7F),
                                   static_cast<std::uint8_t>((i >> 7) & 0x7F)};
    for (int k = 0; k < i % 5; ++k) m.push_back(static_cast<std::uint8_t>((i + k) & 0x7F));
    return m;
}

// The (producer, i) a message encodes, or {-1, -1} if it isn't a well-formed message from encode().
std::pair<int, int> decode(const std::vector<std::uint8_t>& m) {
    if (m.size() < 3 || (m[0] & 0xF0) != 0x90) return {-1, -1};
    const int p = m[0] & 0x0F;
    const int i = m[1] | (m[2] << 7);
    if (p >= kProducers || i >= kPerProducer || m != encode(p, i)) return {-1, -1};
    return {p, i};
}

}  // namespace

TEST_CASE("MidiIo ring: concurrent producers deliver every message intact, once, in order") {
    retroplug::MidiIo midi;
    std::atomic<int> producersLeft{kProducers};

    std::vector<std::thread> producers;
    for (int p = 0; p < kProducers; ++p)
        producers.emplace_back([&midi, &producersLeft, p] {
            for (int i = 0; i < kPerProducer; ++i) {
                const std::vector<std::uint8_t> m = encode(p, i);
                midi.injectForTest(m.data(), m.size());
            }
            producersLeft.fetch_sub(1, std::memory_order_release);
        });

    // The consumer drains while the producers run (the audio thread's shape), then once more after.
    std::vector<retroplug::MidiIo::Message> received;
    std::thread consumer([&] {
        std::vector<retroplug::MidiIo::Message> batch;
        bool last = false;
        while (!last) {
            last = producersLeft.load(std::memory_order_acquire) == 0;
            midi.poll(batch);
            for (auto& m : batch) received.push_back(std::move(m));
        }
    });

    for (auto& t : producers) t.join();
    consumer.join();

    std::set<std::pair<int, int>> seen;
    int lastIndex[kProducers];
    for (int& v : lastIndex) v = -1;
    std::uint64_t lastSeq = 0;
    int malformed = 0, duplicates = 0, reordered = 0, seqBackwards = 0;
    for (const auto& m : received) {
        const auto [p, i] = decode(m.bytes);
        if (p < 0) { ++malformed; continue; }
        if (!seen.insert({p, i}).second) ++duplicates;
        if (i <= lastIndex[p]) ++reordered;
        lastIndex[p] = i;
        if (m.seq <= lastSeq) ++seqBackwards;
        lastSeq = m.seq;
    }

    CHECK(malformed == 0);
    CHECK(duplicates == 0);
    CHECK(reordered == 0);
    CHECK(seqBackwards == 0);
    // Overflow is allowed (a full ring drops and counts), but nothing may vanish uncounted.
    CHECK(received.size() + midi.droppedCount() == static_cast<std::size_t>(kProducers * kPerProducer));
}
