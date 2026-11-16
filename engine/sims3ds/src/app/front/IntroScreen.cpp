/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims front end — see IntroScreen.h
#include "app/front/IntroScreen.h"
#include "app/front/FrontStrings.h"
#include "app/front/FrontUI.h"
#include "app/front/TS1Font.h"
#include "app/front/ResGraphics.h"
#include <algorithm>
#include <cmath>
#include "odh/loading_label.hpp"
#include "files/formats/iff/chunks/STR.h"
#ifdef __3DS__
#include "app/Font.h"
#else
#include "app/ui/OriginalPresentation.h"
#include "linux_text_renderer.hpp"
#endif
#include <cstdio>

namespace simapp { namespace front {

// ======================================================================= intro video (TS1(sims))

IntroVideo::~IntroVideo() { if (_F) fclose((FILE*)_F); }

bool IntroVideo::Open(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    uint8_t h[12];
    if (fread(h, 1, 12, f) != 12 || h[0] != 'S' || h[1] != 'T' || h[2] != 'V' || h[3] != '1') { fclose(f); return false; }
    _W = h[4] | (h[5] << 8); _H = h[6] | (h[7] << 8);
    _Fps = (h[8] | (h[9] << 8)) / 100.0; if (_Fps <= 0) _Fps = 30;
    _Frames = h[10] | (h[11] << 8);
    if (_W <= 0 || _H <= 0 || _W > 400 || _H > 240) { fclose(f); return false; }
    _F = f;
    _Grey.assign((size_t)_W * _H, 0);
    _Index = -1; _T = 0;
    return true;
}

bool IntroVideo::ReadFrame() {
    FILE* f = (FILE*)_F;
    uint8_t sz[4];
    if (fread(sz, 1, 4, f) != 4) return false;
    uint32_t n = sz[0] | (sz[1] << 8) | (sz[2] << 16) | ((uint32_t)sz[3] << 24);
    std::vector<uint8_t> rle(n);
    if (n && fread(rle.data(), 1, n, f) != n) return false;
    size_t o = 0;
    for (size_t i = 0; i + 1 < rle.size() && o < _Grey.size(); i += 2)
        for (int k = 0; k < rle[i] && o < _Grey.size(); k++) _Grey[o++] = rle[i + 1];
    return true;
}

bool IntroVideo::Update(double dtMs) {
    if (!_F) return false;
    _T += dtMs;
    int want = (int)(_T * _Fps / 1000.0);
    while (_Index < want) {
        if (_Index + 1 >= _Frames || !ReadFrame()) return false;
        _Index++;
    }
    return true;
}

void IntroVideo::Draw(Canvas& top) const {
    top.Clear(RGBA(0, 0, 0));
    int ox = (top.Width - _W) / 2, oy = (top.Height - _H) / 2;
    for (int y = 0; y < _H; y++) for (int x = 0; x < _W; x++) {
        uint8_t g = _Grey[(size_t)y * _W + x];
        int dx = ox + x, dy = oy + y;
        if (dx >= 0 && dy >= 0 && dx < top.Width && dy < top.Height) top.Px[(size_t)dy * top.Width + dx] = RGBA(g, g, g);
    }
}

// ======================================================================= LoadingScreen (FreeSims LoadingScreen.cs)

void LoadingScreen::Init(bool desktop) {
    auto& ui = ResGraphics::Get();
    const auto language = static_cast<int>(FSO::Files::Formats::IFF::Chunks::STR::DefaultLangCode);
    const auto artwork = ui.PathFor(ui.Const("kSimsLogo"), language);
    const float scale = desktop ? 1.f : .4f;
    if (artwork.empty() || !ui.LoadScaled(artwork, scale, _Background))
        ui.LoadScaled("Other/setup.bmp", scale, _Background);
}

void LoadingScreen::LoadBootLabels() {
    auto& S = FrontStrings::Get();
    _Copyright = S.Get(154, 5, "\xA9 2000-2005 Electronic Arts Inc. All Rights Reserved.");
    _Brand = S.Get(154, 4, "Maxis is an Electronic Arts brand.");
    // C#: PreloadLabels = 155/6..9. TS1(sims): the game's full set of the funny ones, in the game's order.
    _PreloadLabels.clear();
    for (int i : { 4, 5, 6, 7, 8, 9, 10, 11, 14 }) _PreloadLabels.push_back(S.Get(155, i, ""));
    _PreloadLabels.erase(std::remove(_PreloadLabels.begin(), _PreloadLabels.end(), std::string()), _PreloadLabels.end());
    if (_PreloadLabels.empty()) _PreloadLabels = { "Reticulating Splines", "Interpreting Family Values", "Fabricating Imaginary Infrastructure" };
    _CurrentPreloadLabel = 0;
    _CaptionTimeMs = 0;
    _InTween = false;
    _Label1.clear();
    _Label2 = _PreloadLabels.front();
}

void LoadingScreen::AdvanceBootCaption(double dtMs) {
    if (_PreloadLabels.empty()) return;
    constexpr double interval = 2900.0, transition = 900.0;
    const double cycle = interval * _PreloadLabels.size();
    _CaptionTimeMs = std::fmod(_CaptionTimeMs + std::max(0.0, dtMs), cycle);
    const size_t index = size_t(_CaptionTimeMs / interval);
    _CurrentPreloadLabel = static_cast<int>(index);
    const double dwell = std::fmod(_CaptionTimeMs, interval);
    _InTween = _PreloadLabels.size() > 1 && dwell >= interval - transition;
    _TweenT = _InTween ? (dwell - (interval - transition)) / transition : 0;
    _Label1 = _InTween ? _PreloadLabels[index] : std::string();
    _Label2 = _PreloadLabels[_InTween ? (index + 1) % _PreloadLabels.size() : index];
}

// The expansion artwork is a complete frame. Startup's copyright and rolling
// captions belong only to setup.bmp. Home lots keep the outgoing neighborhood.
void DrawLotLoadStart(Canvas& top, Canvas& bottom, const char* picture, bool showIndicator) {
    if (picture) {
        Canvas art;
        if (ResGraphics::Get().Load(picture, art, false) && art.Width && art.Height) {
            const double scale = std::min(double(top.Width) / art.Width, double(top.Height) / art.Height);
            const int width = int(art.Width * scale + 0.5);
            const int height = int(art.Height * scale + 0.5);
            top.Clear(RGBA(0, 0, 0));
            Rect destination((top.Width-width)/2, (top.Height-height)/2, width, height);
            if (width == art.Width && height == art.Height)
                top.Blit(art.Px.data(), art.Width, art.Height, destination.X, destination.Y, false);
            else
                top.BlitScaled(art.Px.data(), art.Width, art.Height, destination, false);
        } else {
            std::printf("[front] lot loading artwork unavailable: %s\n", picture);
        }
    }
    if (!showIndicator) return;

    const auto language = static_cast<uint8_t>(FSO::Files::Formats::IFF::Chunks::STR::DefaultLangCode);
    const auto label = odh_host::loading_label(language);
    const bool dualScreen = &top != &bottom;
    if (dualScreen)
        bottom.GradientV(0, 0, bottom.Width, bottom.Height, RGBA(0x12,0x2a,0x5a), RGBA(0x09,0x18,0x2f));
    const int width = std::min(bottom.Width, 180), height = 28;
    Rect box((bottom.Width-width)/2, dualScreen ? (bottom.Height-height)/2 : bottom.Height-height-12, width, height);
#ifndef __3DS__
    Canvas badge(width,height);
    badge.Clear(RGBA(0x09,0x18,0x2f));
    auto frame=original::Raster(badge);
    odh_host::TextOverlayStyle style;
    style.x=0;style.y=0;style.width=width;style.height=height;
    style.font_size_px=16;style.centered=true;style.background={0,0,0,0};
    odh_host::draw_text_overlay(frame,label,language,style);
    original::Copy(frame,badge);
    bottom.Blit(badge.Px.data(),width,height,box.X,box.Y,false);
#else
    // The dual-screen interface uses the DVD bitmap font. Like the handheld
    // language chooser, use a readable transcription for unsupported scripts.
    const char* ascii=nullptr;
    switch(language) {
        case 12: ascii="To'en..."; break;
        case 13: ascii="Zagruzka..."; break;
        case 15: ascii="Yomikomi chu..."; break;
        case 17: case 18: ascii="Zhengzai jiazai..."; break;
        case 19: ascii="Kamlang lot..."; break;
        case 20: ascii="Bulleooneun jung..."; break;
    }
    TextCentered(bottom,TS1Size::S12,box,ascii?std::string(ascii):ToLatin1(std::string(label)),RGBA(255,255,255));
#endif
}

void DrawLotLoadBar(Canvas& bottom, float fraction) {
    Rect bar(40, bottom.Height-44, bottom.Width-80, 10);
    bottom.FillRoundRect(bar, 4, RGBA(4,10,30));
    const int width=int((bar.W-4)*std::clamp(fraction,0.0f,1.0f));
    if(width>0) bottom.GradientV(bar.X+2,bar.Y+2,width,bar.H-4,RGBA(120,230,255),RGBA(30,110,200));
    bottom.StrokeRoundRect(bar.X,bar.Y,bar.W,bar.H,4,RGBA(90,130,210));
}

void LoadingScreen::Draw(Canvas& top, Canvas& bottom) {
    bool desktop = &top == &bottom;
    const Canvas& background = _Background;
    // C#: Draw fills the viewport with (0x09,0x18,0x2F), then the container (scaled to the screen height)
    top.Clear(RGBA(0x09, 0x18, 0x2F));
    int bx = (top.Width - background.Width) / 2;
    if (background.Width > 0) {
        top.Blit(background.Px.data(), background.Width, background.Height, bx, 0, false);
        // soft edges where the picture meets the background (C# splashSeg 9-slice edge)
        for (int x = 0; x < 12; x++) {
            int a = 255 - x * 21;
            for (int y = 0; y < top.Height; y++) {
                top.BlendA(bx + x, y, RGBA(0x09, 0x18, 0x2F), a);
                top.BlendA(bx + background.Width - 1 - x, y, RGBA(0x09, 0x18, 0x2F), a);
            }
        }
    }
    const float s = desktop ? 1.0f : 0.4f;
    // lbl.Caption = 154/5, Y = 508, width 800, centred
    TextCentered(top, TS1Size::S9, Rect(0, (int)(508 * s) - 2, top.Width, 14), _Copyright, RGBA(255, 255, 255), RGBA(0, 0, 0, 200));
    // ProgressLabel1/2 at Y = 550, sliding (UITween linear)
    float t = (float)_TweenT;
    int y = (int)(550 * s) - 2;
    int w = top.Width;
    // FIX(sims) QA2: "Fabricating Imaginary Infrastructure" is wider than the top screen at 14 pt (cut at both edges)
    auto labelSize = [&](const std::string& l) { return TextW(TS1Size::S14, l) <= w - 8 ? TS1Size::S14 : TS1Size::S12; };
    if (!_Label1.empty() && _InTween) TextCentered(top, labelSize(_Label1), Rect((int)(-w * t), y, w, 18), _Label1, RGBA(255, 255, 255), RGBA(0, 0, 0, 200));
    TextCentered(top, labelSize(_Label2), Rect((int)(w * (_InTween ? (1.0f - t) : 0.0f)), y, w, 18), _Label2, RGBA(255, 255, 255), RGBA(0, 0, 0, 200));

    // ---- touch screen (TS1(sims))
    if (&top == &bottom) return;
    bottom.GradientV(0, 0, bottom.Width, bottom.Height, RGBA(0x12, 0x2A, 0x5A), RGBA(0x09, 0x18, 0x2F));
    if (_PreloadLabels.empty()) return;
    TextCentered(bottom, TS1Size::S12, Rect(0, 18, bottom.Width, 18), Fit(TS1Size::S12, FrontStrings::Get().Get(153, 0, "The Sims Complete Collection"), bottom.Width - 16), RGBA(255, 216, 96), RGBA(0, 0, 0, 180));
    // the labels shown so far
    int ly = 52;
    for (int i = 0; i <= _CurrentPreloadLabel && i < (int)_PreloadLabels.size(); i++) {
        bool cur = i == _CurrentPreloadLabel;
        std::string l = _PreloadLabels[(size_t)i] + (cur ? std::string("...") : std::string(""));
        Text(bottom, TS1Size::S10, 40, ly, l, cur ? RGBA(255, 255, 255) : RGBA(130, 160, 210), RGBA(0, 0, 0, 150));
        if (!cur) Text(bottom, TS1Size::S10, 18, ly, "\x95", RGBA(120, 220, 90));
        ly += 15;
    }
    // progress bar
    Rect bar(40, 196, bottom.Width - 80, 10);
    bottom.FillRoundRect(bar, 4, RGBA(4, 10, 30));
    int pw = (int)((bar.W - 4) * std::min(1.0f, std::max(0.0f, _Progress)));
    if (pw > 0) bottom.GradientV(bar.X + 2, bar.Y + 2, pw, bar.H - 4, RGBA(120, 230, 255), RGBA(30, 110, 200));
    bottom.StrokeRoundRect(bar.X, bar.Y, bar.W, bar.H, 4, RGBA(90, 130, 210));
    if (!CurrentStep.empty()) TextCentered(bottom, TS1Size::S9, Rect(0, 178, bottom.Width, 14), Fit(TS1Size::S9, CurrentStep, bottom.Width - 20), RGBA(130, 160, 210));
    TextCentered(bottom, TS1Size::S9, Rect(0, 218, bottom.Width, 14), _Brand, RGBA(160, 180, 220));
}

}}
