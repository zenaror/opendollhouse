// SPDX-License-Identifier: MPL-2.0
#pragma once
// Compatibility names for host callers; behavior belongs to the native engine.
// The host selection consumer enqueues through enqueue_source_autonomous,
// which preserves autonomous eligibility intent independently of action args.
// Keeping this facade does not turn the pinned source's default-zero later
// recheck into a proved original executable rule; see InteractionCheckContext.
#include "odh/autonomy_check.hpp"
using odh::validate_saved_autonomy_table;
using odh::saved_autonomy_action_available;
using odh::SavedAutonomyString;
using odh::SavedAutonomyPrimitiveFactory;
using odh::evaluate_saved_autonomy_check;
