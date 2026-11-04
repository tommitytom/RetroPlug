/* The device ABI compiled as C (graph/rp_device.h, graph/rp_jobs.h, graph/rp_stage.h): what a C node binds a link and
 * finishes a job through, so a header that slips C++ in (a default argument, a reference, a namespace) fails this
 * translation unit rather than a pack's. DeviceAbi.test.cpp calls these and compares what C laid out with C++'s. */

#include "DeviceAbi.h"

#include <stdint.h>

void rp_test_c_layout_of(rp_test_c_layout* out) {
    out->request_size = sizeof(rp_job_request);
    out->request_offset = offsetof(rp_job_request, offset);
    out->request_length = offsetof(rp_job_request, length);
    out->request_bytes = offsetof(rp_job_request, bytes);
    out->request_byte_count = offsetof(rp_job_request, size);
    out->devices_size = sizeof(rp_host_devices);
    out->devices_release = offsetof(rp_host_devices, release);
    out->device_jobs_size = sizeof(rp_device_jobs);
    out->device_jobs_submit = offsetof(rp_device_jobs, submit);
    out->host_jobs_size = sizeof(rp_host_jobs);
    out->host_jobs_fail = offsetof(rp_host_jobs, fail);
    out->host_jobs_abandon = offsetof(rp_host_jobs, abandon);
    out->stage_size = sizeof(rp_host_stage);
    out->stage_stage = offsetof(rp_host_stage, stage);
}

void* rp_test_c_bind_and_finish(const rp_host_devices* devices, const rp_host_jobs* reports, rp_job* job) {
    static const uint8_t kResult[3] = {0xC0, 0xDE, 0x01};
    void* link = devices->acquire(devices->ctx, "n8", "rp.n8.cart-link/1");
    reports->progress(job, 1, 3);
    reports->progress(job, 3, 3);
    reports->succeed(job, kResult, sizeof kResult);
    devices->release(devices->ctx, link);
    return link;
}

static bool refuseEveryJob(void* ctx, const rp_job_request* request, rp_job* job, char* error, size_t error_cap) {
    (void)ctx;
    (void)job;
    rp_job_error(error, error_cap, request->op[0] != '\0' ? "a C node has no operation of that name" : "no operation");
    return false;
}

rp_device_jobs rp_test_c_device_jobs(void) {
    rp_device_jobs jobs = {NULL, &refuseEveryJob};
    return jobs;
}
