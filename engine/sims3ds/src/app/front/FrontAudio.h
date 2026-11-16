/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — music / UI sound glue to src/audio (see FrontAudio.cpp). All calls are no-ops without it.
#pragma once
#include <string>
#include <algorithm>

namespace simapp { namespace front {

// DVD sliders have eleven positions. Keep old 0..100 options files readable,
// but convert through the original group-volume table, not linear percentages.
inline int OriginalVolumeStep(int percent) { return (std::clamp(percent,0,100)+5)/10; }
inline float OriginalVolumeGain(int percent) {
    static constexpr int levels[]={0,300,390,490,590,690,790,840,895,960,1024};
    return levels[OriginalVolumeStep(percent)]/1024.0f;
}

/// Music/Modes name: "Load", "Nhood", "dt_nhood", "Vacation", "Superstar", "MakinMagic", "Family", "Credits",
/// "Options", "Live" (no score); anything else fades out.
void FrontMusic(const std::string& mode);
/// UI sound event (FreeSims UISounds names: "ui_click", "ui_nhood_click", "ui_nhood_bdoze_evict" ...).
void FrontSound(const std::string& evt);
/// Options panel volumes, 0..100.
void FrontVolumes(int sfx, int music, int vox);
bool FrontAudioAvailable();
/// ADDED(sims): the EA intro's soundtrack (<cache>/intro.wav): plays `path` ("" stops it). Returns the output
/// latency in ms to hold the first video frame for (A/V sync), or < 0 when nothing plays (no audio / no file).
double FrontIntroSound(const std::string& path);
/// A lot was unloaded: stop its objects' sounds (GameAudio::LeaveLot).
void FrontLotUnloaded();

}}
