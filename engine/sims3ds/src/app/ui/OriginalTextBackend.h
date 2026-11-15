/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
namespace odh_host { struct TextOverlayBackend; }
namespace simapp { namespace original {
// Uses the current game's font service where a host shaper is not installed.
void InstallTextBackendIfNeeded();
#ifndef __3DS__
odh_host::TextOverlayBackend GameFontTextBackend();
#endif
}}
