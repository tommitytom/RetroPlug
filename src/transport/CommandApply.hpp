#pragma once

class SystemBase;
struct Command;

// Apply one per-system mutation command to an already-resolved system. Single
// source of truth for the backend-agnostic command handlers (LoadState,
// LoadSram, NewSram, ResetSystem) shared by the DSP run loop (PluginDSP::run),
// the concurrency stress harness, and the unit tests — so the handler logic
// (ownership/free of the heap payload, the LoadSram-resets-but-LoadState-does-
// not contract, the projectMutated flag) lives in exactly one place.
//
// `sys` is the resolved target (or nullptr if the id no longer exists). For the
// load commands the heap-allocated `bytes` payload is always freed, even when
// `sys` is nullptr. `projectMutated` is set true when the command changed
// serialized project state (matching the DSP loop's existing semantics — note
// ResetSystem does NOT set it). Other command kinds are ignored.
void applySystemCommand(SystemBase* sys, const Command& cmd, bool& projectMutated);
