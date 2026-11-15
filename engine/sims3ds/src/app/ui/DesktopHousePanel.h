/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
#ifndef __3DS__
#include "live_panel_view.hpp"
namespace simapp { class LotSession; namespace original {
// No alternate VM. Absence means the active session is not the family's home.
std::optional<odh_host::LiveHouseInfo> DesktopHouseSnapshot(LotSession&);
}}
#endif
