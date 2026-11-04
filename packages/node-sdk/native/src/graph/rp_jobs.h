/* rp_jobs.h - capability jobs a device's node runs (docs/modular-graph-phase-e2-design.md section 5, the job model of
 * docs/modular-graph-phase-c5-design.md section 6).
 *
 * The control plane asks a node of the running graph to do something to the system behind it - a read, a write, a
 * dump - and the answer comes later. A host keeps a record per job; a device's node is handed the request and the
 * record's opaque handle, takes the job or refuses it at once, and a job it takes is finished through the host's report
 * interface below, from whichever thread runs it (a device's own, for an N8 cart's seconds of USB transfers). Plain C
 * so a C node can present one; the record's internals are the host's alone.
 *
 * Two rules a device's jobs keep, beside whatever bound the host puts on its records:
 *
 *   - One job at a time per device. A device's node refuses a second at submission ("busy: ..."), and one while the
 *     device is not attached ("not connected"), synchronously, before the host keeps a record. A node leaving the plan
 *     abandons its job (rp_host_jobs.abandon), which fails it at once; the device reports into it no more.
 *   - Progress overwrites. A device reports its latest progress into the record, and the host hands a listener the
 *     value it finds when it next looks: a job that reports in a tight loop accumulates nothing. */
#ifndef RP_JOBS_H
#define RP_JOBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The three capabilities a job may name, as the descriptors spell them: a node presents an rp_device_jobs under each it
 * answers, through its get_interface. */
#define RP_SYSTEM_MEMORY_INTERFACE "rp.system.memory/1"
#define RP_SYSTEM_SRAM_INTERFACE "rp.system.sram/1"
#define RP_SYSTEM_ROM_INTERFACE "rp.system.rom/1"

/* What the control plane asks for, valid for the length of the submit call only: a device that needs any of it later
 * (the bytes it uploads, the file name it reaches) copies it. Every string is non-null, NUL-terminated and UTF-8, empty
 * when the request does not carry it; `bytes` is null when `size` is 0, so a device copies only after it checks the
 * size; the arguments an operation does not read are ignored. A host refuses a request whose
 * source strings hold a NUL byte rather than build one of these from it: a C string ends there, so the device would
 * read a battery name "a\0b" as "a" and reach another game's file.
 *
 * Versioning: the three capability ids below version this struct and rp_device_jobs, each presented under its own id;
 * rp.host.jobs/1 versions the report table alone. A field added to either struct is a new version of the capability
 * ids, never an edit under the old ones. */
typedef struct rp_job_request {
    const char*    node_id;     /* a node of the host's last published plan */
    const char*    capability;  /* one of the ids above */
    const char*    op;          /* memory: regions, read, write; sram: dump, restore; rom: header, title, upload */
    const char*    region;      /* memory: the region's name */
    const char*    name;        /* rom upload: the file name the image is stored under on the device; sram on a cart:
                                   the ROM file name whose battery file the job reaches */
    uint32_t       offset;      /* memory: where a read or write starts in the region */
    uint32_t       length;      /* memory read, rom header: how many bytes (0: to the end) */
    const uint8_t* bytes;       /* memory write, sram restore, rom upload */
    size_t         size;
} rp_job_request;

/* The host's record of one job, opaque to a node: what a submit is handed and every report names. Every host keeps
 * rp_job incomplete: its record is a type of its own, cast to and from rp_job*; a host never defines struct rp_job. */
typedef struct rp_job rp_job;

/* What a device's node presents under each capability id it answers. [control] `submit` takes the job into `job` or
 * refuses it, false with a NUL-terminated UTF-8 reason in `error` (at most error_cap bytes, the NUL included): "busy",
 * "not connected", an operation it does not have, arguments it will not take. A refusal leaves the record untouched.
 * A job taken is the device's to finish, through the host's rp_host_jobs: on its own thread, or at once, before submit
 * returns (a read the device's host answers from what it keeps). */
typedef struct rp_device_jobs {
    void* ctx;
    bool (*submit)(void* ctx, const rp_job_request* request, rp_job* job, char* error, size_t error_cap);
} rp_device_jobs;

/* The host's report interface, through rp_host.get_interface: how a device finishes the jobs it takes. A node takes it
 * in bind_host, as it takes rp.host.devices, and hands it with each job to whatever runs the job (its device link); a
 * host that offers none takes no device jobs, and its device nodes refuse every one. There is no ctx: the record is
 * the whole of what a report reaches, so one interface serves every record a host makes.
 *
 * [any thread], one job by one thread at a time: whoever finishes the job reports into it, and nothing touches a
 * record after its terminal report (succeed, fail or abandon), which the host may free from then on. progress may come
 * any number of times before that. */
#define RP_HOST_JOBS_INTERFACE "rp.host.jobs/1"
typedef struct rp_host_jobs {
    /* The progress so far, `done` of `total` (bytes, for a transfer). Overwrites. */
    void (*progress)(rp_job* job, uint32_t done, uint32_t total);
    /* Finish the job with its result: the host copies `size` bytes from `data` (null when size is 0). */
    void (*succeed)(rp_job* job, const uint8_t* data, size_t size);
    /* Finish the job with the reason it failed: NUL-terminated UTF-8, which the host copies, keeping at most its first
     * 1024 bytes (the engine's bound) and never a sequence that bound would split. */
    void (*fail)(rp_job* job, const char* reason);
    /* [control] The node that submitted the job is leaving the plan: the job fails as its node's removal, the same
     * failure the host gives a job whose node is gone, and the device reports into it no more. A device that reports
     * from a thread of its own serialises those reports with abandon (DeviceJobSlot does, under one lock), so that
     * after abandon returns nothing reaches the record, which the host may then free. */
    void (*abandon)(rp_job* job);
} rp_host_jobs;

/* Write `text` into a submit's error buffer: as much as fits, always NUL-terminated, nothing when there is no room. A
 * cut never ends inside a UTF-8 sequence (the reason crosses the RPC as a string), so it backs up to the start of the
 * code point it would have split. __inline for MSVC's C, whose default dialect has no `inline`. */
#if defined(_MSC_VER) && !defined(__cplusplus)
#define RP_JOBS_INLINE static __inline
#else
#define RP_JOBS_INLINE static inline
#endif
RP_JOBS_INLINE void rp_job_error(char* error, size_t error_cap, const char* text) {
    if (error == NULL || error_cap == 0) return;
    size_t n = 0;
    if (text != NULL)
        while (n + 1 < error_cap && text[n] != '\0') {
            error[n] = text[n];
            ++n;
        }
    if (text != NULL && text[n] != '\0')
        while (n > 0 && ((unsigned char)text[n] & 0xC0u) == 0x80u) --n;
    error[n] = '\0';
}

#ifdef __cplusplus
}
#endif

#endif /* RP_JOBS_H */
