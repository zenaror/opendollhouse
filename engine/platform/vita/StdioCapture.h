/* SPDX-License-Identifier: MPL-2.0 */
#pragma once

namespace odh::vita {
// Capture both the SDK's legacy streams and this calling thread's newlib
// streams. Call once on the application thread before starting SDL threads.
// Failure is reported through the native debug output and an exception.
void CaptureStdio(const char* path);

}
