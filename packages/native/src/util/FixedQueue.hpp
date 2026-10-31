#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

// The queues a block feeds, held in storage sized before the block runs (docs/modular-graph-phase-c4-design.md
// section 4). A core's ingress queues are filled on the block thread, so they may not grow there: each is reserved to
// the most a block can deliver when the core is built, and a push that does not fit is dropped and counted rather than
// allocating. The bound is the one a graph queue has (a push that does not fit is dropped and counted).
//
// One buffer and a head index rather than a deque: a deque allocates a node on its first push (or at construction, on
// libstdc++), and allocates another as it grows, on whichever thread first reaches that depth.

namespace rp {

template <class T>
class FixedQueue {
public:
    FixedQueue() = default;
    explicit FixedQueue(std::size_t capacity) { reserve(capacity); }

    // Room for `capacity` entries. Growth only, and only where allocating is allowed (a core's construction, an
    // activation): a queue never grows while a block runs.
    void reserve(std::size_t capacity) {
        if (capacity <= capacity_) return;
        buffer_.reserve(capacity);
        capacity_ = capacity;
    }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return buffer_.size() - head_; }
    bool empty() const { return head_ == buffer_.size(); }

    // Append one entry. False, appending nothing, when the queue holds capacity() entries: the caller decides what a
    // dropped push means for its stream. A queue whose overflow is silent reads dropped() instead.
    bool push(const T& value) {
        if (buffer_.size() == capacity_) {
            if (head_ == 0) {
                ++dropped_;
                return false;
            }
            // The consumed prefix takes the room the new entry needs, so the buffer's own size stays the bound.
            buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(head_));
            head_ = 0;
            // Unreachable after the erase above (head_ > 0 left at least one slot free), kept as a guard: a queue that
            // did reach capacity here must still refuse rather than grow past its bound.
            if (buffer_.size() == capacity_) {
                ++dropped_;
                return false;
            }
        }
        buffer_.push_back(value);
        return true;
    }

    T&       front() { return buffer_[head_]; }
    const T& front() const { return buffer_[head_]; }
    T&       back() { return buffer_.back(); }
    const T& back() const { return buffer_.back(); }
    void     pop() {
        ++head_;
        if (head_ == buffer_.size()) clear();
    }
    void clear() {
        buffer_.clear();
        head_ = 0;
    }

    std::uint64_t dropped() const { return dropped_; }

    // The live entries, oldest first, for a pass over what one block queued (a rebase, a sort before the drain).
    T*       begin() { return buffer_.data() + head_; }
    T*       end() { return buffer_.data() + buffer_.size(); }
    const T* begin() const { return buffer_.data() + head_; }
    const T* end() const { return buffer_.data() + buffer_.size(); }
    T&       operator[](std::size_t i) { return buffer_[head_ + i]; }
    const T& operator[](std::size_t i) const { return buffer_[head_ + i]; }

private:
    std::vector<T> buffer_;
    std::size_t    head_     = 0;
    std::size_t    capacity_ = 0;
    std::uint64_t  dropped_  = 0;
};

// A queue of bytes read from the front, held in a ring of fixed capacity: a byte stream that is only ever consumed in
// the order it arrived (a serial port's input), so nothing has to move and nothing can grow.
class FixedByteRing {
public:
    FixedByteRing() = default;
    explicit FixedByteRing(std::size_t capacity) { reserve(capacity); }

    void reserve(std::size_t capacity) {
        if (capacity <= capacity_) return;
        bytes_.resize(capacity);
        capacity_ = capacity;
    }
    std::size_t capacity() const { return capacity_; }
    std::size_t size() const { return size_; }
    bool        empty() const { return size_ == 0; }
    std::uint8_t front() const { return bytes_[head_]; }

    bool push(std::uint8_t byte) {
        if (size_ == capacity_ || capacity_ == 0) {
            ++dropped_;
            return false;
        }
        bytes_[(head_ + size_) % capacity_] = byte;
        ++size_;
        return true;
    }
    void pop() {
        if (size_ == 0) return;
        head_ = (head_ + 1) % capacity_;
        --size_;
    }
    void clear() {
        head_ = 0;
        size_ = 0;
    }
    std::uint64_t dropped() const { return dropped_; }

private:
    std::vector<std::uint8_t> bytes_;
    std::size_t               head_     = 0;
    std::size_t               size_     = 0;
    std::size_t               capacity_ = 0;
    std::uint64_t             dropped_  = 0;
};

// Stable sort of a block's queued entries by offset, in place and allocating nothing: std::stable_sort takes a
// temporary buffer, which the block thread may not have. The queue is built from one or two feeds, each already in
// offset order, so the insertion pass costs about one run of the second feed rather than a sort (the same trade
// Engine::Staging::sortByFrame makes for the host's staged MIDI). A producer that stamps decreasing offsets is
// allowed, though, and the N8 role's queue holds 4352 runs: that worst case is about 9 M entry moves, quadratic on
// the block thread, which is why the feeds' order is the thing to keep.
template <class T, class Less>
void stableSortEntries(T* first, T* last, Less less) {
    for (T* i = first + 1; i < last; ++i)
        for (T* j = i; j > first && less(*j, *(j - 1)); --j) std::swap(*j, *(j - 1));
}

}  // namespace rp
