// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "odh/session_bundle.hpp"
namespace odh {
// Validated durable projection into exact resource-authenticated HOME entities.
// The prior snapshot is the per-house baseline. Only newer writer revisions
// apply; local roles, needs, routes, threads and entity identities stay intact.
// Missing bodies are legitimate: the shared owner serves later constructors.
// Pure candidate operation; failure leaves input unchanged.
void project_native_personal_session(SessionBundle&,const NativePersonalSnapshot&);
// Same operation for a fully authenticated restored live World, atomically.
// The source bundle supplies exact HHRD/NPCA resources and previous revision.
bool publish_native_personal_session(World&,const SessionBundle&,const NativePersonalSnapshot&);
} // namespace odh
