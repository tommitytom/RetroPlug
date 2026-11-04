// The device ABI's C headers (graph/rp_device.h, rp_jobs.h and rp_stage.h, docs/modular-graph-phase-e2-design.md
// section 5): compiled as C by DeviceAbi.c and laid out there as C++ lays them out, a C node's binding and job driven
// through an engine's registry and report interface, and rp.host.jobs/1 itself (the reports a device finishes the
// engine's record through, and DeviceJobSlot's rule that a report after an abandon reaches nothing).
//
// Run via `pnpm test:plugin engine-graph`.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "DeviceAbi.h"
#include "graph/desktop/DeviceJobSlot.hpp"
#include "graph/rp_device.h"
#include "graph/rp_jobs.h"
#include "host/devices/DeviceRegistry.hpp"
#include "host/engine/CapabilityJobs.hpp"

namespace {

using rp::host::Job;
using Bytes = std::vector<std::uint8_t>;

int acquired = 0, released = 0;
void countAcquire(void*) { ++acquired; }
void countRelease(void*) { ++released; }

}  // namespace

TEST_CASE("the device ABI compiles as C, and C lays it out as C++ does", "[devices]") {
    rp_test_c_layout c{};
    rp_test_c_layout_of(&c);
    CHECK(c.request_size == sizeof(rp_job_request));
    CHECK(c.request_offset == offsetof(rp_job_request, offset));
    CHECK(c.request_length == offsetof(rp_job_request, length));
    CHECK(c.request_bytes == offsetof(rp_job_request, bytes));
    CHECK(c.request_byte_count == offsetof(rp_job_request, size));
    CHECK(c.devices_size == sizeof(rp_host_devices));
    CHECK(c.devices_release == offsetof(rp_host_devices, release));
    CHECK(c.device_jobs_size == sizeof(rp_device_jobs));
    CHECK(c.device_jobs_submit == offsetof(rp_device_jobs, submit));
    CHECK(c.host_jobs_size == sizeof(rp_host_jobs));
    CHECK(c.host_jobs_fail == offsetof(rp_host_jobs, fail));
    CHECK(c.host_jobs_abandon == offsetof(rp_host_jobs, abandon));
    CHECK(c.stage_size == sizeof(rp_host_stage));
    CHECK(c.stage_stage == offsetof(rp_host_stage, stage));
}

TEST_CASE("a C node binds its link and finishes a job through the engine's registry and reports", "[devices]") {
    acquired = released = 0;
    rp::host::DeviceRegistry registry;
    int                      link = 0;  // the link's identity is all the C side reads of it
    REQUIRE(registry.install("n8", "rp.n8.cart-link/1", &link, &countAcquire, &countRelease));

    Job job;
    CHECK(rp_test_c_bind_and_finish(registry.api(), rp::host::jobReports(), rp::host::handle(&job)) == &link);
    CHECK(acquired == 1);
    CHECK(released == 1);
    // The record behind the rp_job the C side named, finished as the engine's own: progress overwritten to the last,
    // the result copied, the terminal state last.
    CHECK(job.state.load() == Job::Done);
    CHECK(job.progress.load() == 3);
    CHECK(job.total.load() == 3);
    CHECK(job.result == Bytes{0xC0, 0xDE, 0x01});

    // A C node's refusal comes back in the caller's buffer, NUL-terminated and cut to fit.
    const rp_device_jobs jobs = rp_test_c_device_jobs();
    const rp::host::JobRequest r{"cart", RP_SYSTEM_SRAM_INTERFACE, "dump", "", 0, 0, {}, ""};
    const rp_job_request       request = rp::host::requestView(r);
    char                       error[256] = {};
    Job                        refused;
    CHECK_FALSE(jobs.submit(jobs.ctx, &request, rp::host::handle(&refused), error, sizeof error));
    CHECK(std::string(error) == "a C node has no operation of that name");
    CHECK(refused.state.load() == Job::Queued);  // a refusal leaves the record untouched
    char small[6] = {'x', 'x', 'x', 'x', 'x', 'x'};
    CHECK_FALSE(jobs.submit(jobs.ctx, &request, rp::host::handle(&refused), small, sizeof small));
    CHECK(std::string(small) == "a C n");
    rp_job_error(small, 0, "nothing fits");  // no room: nothing is written
    CHECK(std::string(small) == "a C n");
    rp_job_error(small, sizeof small, nullptr);  // no text: an empty reason
    CHECK(small[0] == '\0');
}

TEST_CASE("rp.host.jobs/1: progress overwrites, succeed copies the result, fail keeps the reason", "[capability]") {
    const rp_host_jobs* reports = rp::host::jobReports();

    Job running;
    reports->progress(rp::host::handle(&running), 10, 100);
    reports->progress(rp::host::handle(&running), 40, 100);
    CHECK(running.progress.load() == 40);  // the latest, not a sum
    CHECK(running.total.load() == 100);
    CHECK_FALSE(running.finished());

    Job done;
    {
        Bytes result{1, 2, 3, 4};
        reports->succeed(rp::host::handle(&done), result.data(), result.size());
        result.assign(4, 0xFF);  // the caller's bytes are its own again the moment the report returns
    }
    CHECK(done.state.load() == Job::Done);
    CHECK(done.result == Bytes{1, 2, 3, 4});
    Job empty;
    reports->succeed(rp::host::handle(&empty), nullptr, 0);
    CHECK(empty.state.load() == Job::Done);
    CHECK(empty.result.empty());

    Job failed;
    {
        std::string reason = "the cart said no";
        reports->fail(rp::host::handle(&failed), reason.c_str());
        reason.assign("overwritten");
    }
    CHECK(failed.state.load() == Job::Failed);
    CHECK(failed.failure.load() == Job::Device);
    CHECK(failed.error == "the cart said no");

    Job abandoned;
    reports->abandon(rp::host::handle(&abandoned));
    CHECK(abandoned.state.load() == Job::Failed);
    CHECK(abandoned.failure.load() == Job::Removed);  // the failure the engine gives a job whose node is gone
    CHECK(abandoned.error.empty());
}

TEST_CASE("a device's report after its node abandoned the job reaches nothing", "[capability]") {
    rp::DeviceJobSlot slot;
    int               owner = 0, other = 0;
    Job               job;
    REQUIRE(slot.claim(rp::host::jobReports(), rp::host::handle(&job), &owner));
    Job second;
    CHECK_FALSE(slot.claim(rp::host::jobReports(), rp::host::handle(&second), &owner));  // one job at a time
    slot.progress(5, 10);
    CHECK(job.progress.load() == 5);

    // Another node leaving the plan touches nothing of this job's.
    slot.abandon(&other);
    CHECK_FALSE(job.finished());
    CHECK_FALSE(slot.abandoned());

    // Its own node leaving fails it at once, and the slot stays busy while the device is still at work.
    slot.abandon(&owner);
    CHECK(job.state.load() == Job::Failed);
    CHECK(job.failure.load() == Job::Removed);
    CHECK(slot.abandoned());
    CHECK(slot.busy());

    // What the device reports from here on reaches no record: the engine may already have freed it.
    slot.progress(9, 10);
    slot.succeed(Bytes{7, 7});
    CHECK(job.progress.load() == 5);
    CHECK(job.result.empty());
    CHECK(job.failure.load() == Job::Removed);
    // The device's finish frees the slot for the next job.
    CHECK_FALSE(slot.busy());
    Job next;
    REQUIRE(slot.claim(rp::host::jobReports(), rp::host::handle(&next), &owner));
    slot.fail("the next one's own reason");
    CHECK(next.error == "the next one's own reason");
    CHECK(next.failure.load() == Job::Device);

    // A claim given back before the job started leaves its record untouched.
    Job unstarted;
    REQUIRE(slot.claim(rp::host::jobReports(), rp::host::handle(&unstarted), &owner));
    slot.unclaim();
    CHECK(unstarted.state.load() == Job::Queued);
    CHECK_FALSE(slot.busy());
}
