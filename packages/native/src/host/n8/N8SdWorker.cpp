#include "host/n8/N8SdWorker.hpp"

#include <exception>
#include <utility>

namespace retroplug {

void N8SdWorker::Progress::phase(const std::string& p) {
    { std::lock_guard<std::mutex> lk(w_.meta_); w_.phase_ = p; }
    w_.bump();
}
void N8SdWorker::Progress::total(std::uint64_t n) {
    w_.bytesTotal_.store(n, std::memory_order_relaxed);
    w_.bump();
}
void N8SdWorker::Progress::advance(std::uint64_t n) {
    w_.bytesDone_.fetch_add(n, std::memory_order_relaxed);
    w_.bump();
}
void N8SdWorker::Progress::result(const std::string& r) {
    { std::lock_guard<std::mutex> lk(w_.meta_); w_.result_ = r; }
    w_.bump();
}

N8SdWorker::~N8SdWorker() {
    if (thread_.joinable()) thread_.join();
}

bool N8SdWorker::start(const std::string& op, Job job) {
    bool expected = false;
    if (!busy_.compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return false;  // a job is already in flight
    if (thread_.joinable()) thread_.join();  // reap the previous (finished) job's thread

    {
        std::lock_guard<std::mutex> lk(meta_);
        op_ = op;
        phase_.clear();
        error_.clear();
        result_.clear();
    }
    bytesDone_.store(0, std::memory_order_relaxed);
    bytesTotal_.store(0, std::memory_order_relaxed);
    done_.store(false, std::memory_order_relaxed);
    bump();

    thread_ = std::thread([this, job = std::move(job)]() {
        Progress p(*this);
        try {
            job(p);
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lk(meta_);
            error_ = e.what();
        } catch (...) {
            std::lock_guard<std::mutex> lk(meta_);
            error_ = "unknown error";
        }
        done_.store(true, std::memory_order_release);
        busy_.store(false, std::memory_order_release);
        bump();
    });
    return true;
}

N8SdStatusDto N8SdWorker::status() {
    N8SdStatusDto s;
    s.busy       = busy_.load(std::memory_order_acquire);
    s.bytesDone  = bytesDone_.load(std::memory_order_relaxed);
    s.bytesTotal = bytesTotal_.load(std::memory_order_relaxed);
    s.done       = done_.load(std::memory_order_acquire);
    s.version    = version_.load(std::memory_order_acquire);
    std::lock_guard<std::mutex> lk(meta_);
    s.op     = op_;
    s.phase  = phase_;
    s.error  = error_;
    s.result = result_;
    return s;
}

}  // namespace retroplug
