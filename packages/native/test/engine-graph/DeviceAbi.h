/* What DeviceAbi.c, compiled as C, offers DeviceAbi.test.cpp: one declaration of each, read by both languages, so the
 * layout the two compare cannot drift between them. */
#ifndef RP_TEST_DEVICE_ABI_H
#define RP_TEST_DEVICE_ABI_H

#include <stddef.h>

#include "graph/rp_device.h"
#include "graph/rp_jobs.h"
#include "graph/rp_stage.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Where C puts each field the headers' structs carry, for the C++ side to compare with its own offsetof. */
typedef struct rp_test_c_layout {
    size_t request_size, request_offset, request_length, request_bytes, request_byte_count;
    size_t devices_size, devices_release;
    size_t device_jobs_size, device_jobs_submit;
    size_t host_jobs_size, host_jobs_fail, host_jobs_abandon;
    size_t stage_size, stage_stage;
} rp_test_c_layout;

void rp_test_c_layout_of(rp_test_c_layout* out);
/* A C device node's whole use of the two interfaces: bind the link "n8" names, then finish a job through the host's
 * reports - two progress reports and its three result bytes - and give the link back. Answers what the acquire did. */
void* rp_test_c_bind_and_finish(const rp_host_devices* devices, const rp_host_jobs* reports, rp_job* job);
/* The submit a C node presents: it refuses with its reason in the caller's buffer, cut to fit. */
rp_device_jobs rp_test_c_device_jobs(void);

#ifdef __cplusplus
}
#endif

#endif /* RP_TEST_DEVICE_ABI_H */
