/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — see UIMotiveDisplay.h (FreeSims UI/Controls/UIMotiveDisplay.cs)
#include "app/live/UIMotiveDisplay.h"
#include "app/Font.h"
#include <algorithm>
#include <cmath>

namespace simapp {

static const char* DefaultNames[8] = { "Hunger", "Comfort", "Hygiene", "Bladder", "Energy", "Fun", "Social", "Room" };
// Both selected Simitone UIMotiveBar references use 240 samples at 60 Hz.
// UI time drives those samples, never the number of rendered frames.
static const size_t CHANGE_WINDOW = 240;
static constexpr double SAMPLE_MS = 1000.0 / 60.0;
static constexpr double MAX_GAP_MS = 9000.0; // four-second history + full five-second fade
static constexpr double TICK_EPSILON = 1e-7;

UIMotiveDisplay::UIMotiveDisplay() {
    for (int i = 0; i < 8; i++) MotiveNames[i] = DefaultNames[i];
}

void UIMotiveDisplay::Reset() {
    FirstFrame = true; ClockReady = false; FixedTick = 0;
    OriginMs = LastTimeMs = 0; Pending.clear();
    for (auto& history : ChangeBuffer) history.clear();
    for (int i=0;i<8;++i) ArrowStates[i]=TargetArrowStates[i]=0;
}

void UIMotiveDisplay::Seed(double timeMs) {
    Reset(); OriginMs=LastTimeMs=timeMs; ClockReady=true; FirstFrame=false;
    for(int i=0;i<8;++i) OldMotives[i]=SampledValues[i]=MotiveValues[i];
}

void UIMotiveDisplay::Update() {
    // Compatibility for callers of the old 30-Hz API: two canonical 60-Hz
    // steps per call retain its window, count and fade exactly.
    if(!ClockReady){Update(0);return;}
    const double first=OriginMs+double(FixedTick+1)*SAMPLE_MS;
    const double second=OriginMs+double(FixedTick+2)*SAMPLE_MS;
    Update(first); Update(second);
}

void UIMotiveDisplay::Update(double timeMs) {
    if(!std::isfinite(timeMs)||timeMs<0||timeMs+SAMPLE_MS==timeMs){Reset();return;}
    if(!ClockReady||timeMs<LastTimeMs||timeMs-LastTimeMs>MAX_GAP_MS){Seed(timeMs);return;}
    LastTimeMs=timeMs;
    const double position=(timeMs-OriginMs)/SAMPLE_MS;
    // Rebase an unrepresentable timeline rather than overflow a tick index.
    if(!std::isfinite(position)||position>9e15){Seed(timeMs);return;}
    const auto due=uint64_t(std::floor(position+TICK_EPSILON));
    const auto observed=std::max(FixedTick+1,uint64_t(std::ceil(position-TICK_EPSILON)));
    Sample sample{observed,{}};
    for(int i=0;i<8;++i)sample.Values[i]=MotiveValues[i];
    // Multiple redraws / inputs inside one canonical sample keep the newest
    // values, without extra history or animation steps.
    if(!Pending.empty()&&Pending.back().Tick==observed)Pending.back()=sample;
    else Pending.push_back(sample);
    while(FixedTick<due){
        ++FixedTick;
        while(!Pending.empty()&&Pending.front().Tick<=FixedTick){
            SampledValues=Pending.front().Values;Pending.pop_front();
        }
        Step();
    }
}

void UIMotiveDisplay::Step() {
    for (int i = 0; i < 8; i++) {
        ChangeBuffer[i].push_back(SampledValues[i] - OldMotives[i]);
        if (ChangeBuffer[i].size() > CHANGE_WINDOW) ChangeBuffer[i].pop_front();

        int sum = 0;
        for (auto c : ChangeBuffer[i]) sum += c;

        double diff = sum / 2.5;
        if (diff < 0) diff = std::floor(diff);
        else if (diff > 0) diff = std::ceil(diff);
        TargetArrowStates[i] = std::max(std::min((int)diff, 5), -5) * 60;

        OldMotives[i] = SampledValues[i];
    }

    for (int i = 0; i < 8; i++) {
        {
            if (TargetArrowStates[i] > ArrowStates[i]) ArrowStates[i]++;
            else if (TargetArrowStates[i] < ArrowStates[i]) ArrowStates[i]--;
        }
    }
}

static inline uint32_t Col(double r, double g, double b, int a = 255) { return RGBA((int)r, (int)g, (int)b, a); }

void UIMotiveDisplay::DrawMotive(Canvas& c, int x, int y, int w, int h, int motive, bool labelAbove) {
    double p = (MotiveValues[motive] + 100) / 200.0;
    p = std::max(0.0, std::min(1.0, p));
    uint32_t barcol = Col(57 * (1 - p), 213 * p + 97 * (1 - p), 49 * p + 90 * (1 - p));
    uint32_t bgcol = Col(57 * p + 214 * (1 - p), 97 * p, 90 * p);

    int labelW = labelAbove ? 0 : std::min(58, w / 3 + 8);
    int bx = x + labelW, bw = w - labelW - 14;
    int by = labelAbove ? y + 14 : y + (h - 7) / 2, bh = labelAbove ? std::max(5, std::min(10, h - 18)) : 7;
    c.FillRect(bx, by, bw, bh, bgcol);
    c.FillRect(bx, by, (int)(bw * p), bh, barcol);
    c.FillRect(bx + (int)(bw * p), by, 1, bh, RGBA(0, 0, 0));
    c.FillRect(bx, by, bw, 1, RGBA(255, 255, 255, 50));

    // label (shadowed, like the C# style with a black copy offset by 1)
    std::string name = Ellipsize(FontId::Small, MotiveNames[motive], labelAbove ? bw : labelW - 2);
    if (labelAbove) {
        int tw = TextWidth(FontId::Small, name);
        DrawTextShadow(c, FontId::Small, bx + (bw - tw) / 2, y, name, RGBA(255, 255, 255), RGBA(0, 0, 0));
    } else {
        DrawTextShadow(c, FontId::Small, x, y + (h - LineHeight(FontId::Small)) / 2, name, RGBA(255, 255, 255), RGBA(0, 0, 0));
    }

    // change arrows: green to the right of the bar when rising, red to the left when falling
    int arrowState = ArrowStates[motive];
    int ay = by + bh / 2;
    if (arrowState > 0) {
        for (int i = 0; i < (int)std::ceil(arrowState / 60.0); i++) {
            int a = (int)(255 * std::min(1.0, arrowState / 60.0 - i));
            int ax = bx + bw + 2 + i * 4;
            if (ax + 3 > x + w) break;
            c.FillTriangle(ax, ay - 3, ax, ay + 3, ax + 3, ay, RGBA(0x00, 0xCB, 0x39, a));
        }
    } else if (arrowState < 0) {
        arrowState = -arrowState;
        for (int i = 0; i < (int)std::ceil(arrowState / 60.0); i++) {
            int a = (int)(255 * std::min(1.0, arrowState / 60.0 - i));
            int ax = bx - 4 - i * 4;
            if (labelAbove ? ax < x : ax < x + labelW - 12) break;
            c.FillTriangle(ax + 3, ay - 3, ax + 3, ay + 3, ax, ay, RGBA(0xD6, 0x00, 0x00, a));
        }
    }
}

void UIMotiveDisplay::Draw(Canvas& c, const Rect& r) {
    int colW = r.W / 2;
    int rowH = r.H / 4;
    bool labelAbove = rowH >= 28;
    for (int i = 0; i < 4; i++) {
        DrawMotive(c, r.X + 4, r.Y + rowH * i, colW - 8, rowH, i, labelAbove);          //left side
        DrawMotive(c, r.X + colW + 4, r.Y + rowH * i, colW - 8, rowH, i + 4, labelAbove); //right side
    }
}

}
