// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <functional>
#include <string>

namespace odh {
using InstallEntryVisitor = std::function<void(const std::string& name, bool directory)>;
// On a miss, return false without visiting entries. A hit includes the whole
// directory, including an empty one. Callers retain their own traversal limits.
using LiveInstallDirectory = bool (*)(const std::string& path, const InstallEntryVisitor& visit);
}

namespace FSO::Files {
// Main-thread, read-only installation inventory collected during this run.
bool VisitLiveInstallDirectory(const std::string& path, const odh::InstallEntryVisitor& visit);
}
