// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <algorithm>
#include <cstdint>

namespace FSO { namespace Common { namespace TS1 {

// DVD46d7b3,46daf2,4733da..47342a. This is a wall-time dispatch
// budget, not saved simulation time. Both ordinary and restricted scans use it.
class DispatchCadence {
public:
    static int ClampRaw(int raw) { return std::max(0, std::min(1000, raw)); }
    static uint32_t PeriodForRaw(int raw) {
        // The original uses 24-bit x87 arithmetic. Keep each operation rounded
        // to float, rather than fusing it or evaluating the chain in double.
        volatile float scaled = float(ClampRaw(raw)) * 0.001f;
        volatile float product = scaled * 171.0f;
        volatile float remaining = 172.0f - product;
        volatile float rounded = remaining + 0.5f;
        return uint32_t(rounded);
    }
    void SetRawSpeed(int raw) {
        _Raw = ClampRaw(raw);
        _Period = PeriodForRaw(_Raw);
        ResetOwner(); // Changing token period preserves its balance and epoch.
    }
    void ResetOwner() { _OwnerStarted = false; _Backlog = 0; }
    int RawSpeed() const { return _Raw; }
    uint32_t Period() const { return _Period; }

    // The app supplies elapsed milliseconds from its frame clock. Clamp the
    // global clock advance just as473622 does, before sampling either budget.
    void BeginFrame(double elapsedMs) {
        if (!(elapsedMs >= 0)) elapsedMs = 0;
        _Fraction += std::min(2000.0, elapsedMs);
        const auto elapsed = uint32_t(_Fraction);
        _Fraction -= elapsed;
        _Now += elapsed;
        if (_OwnerStarted) _Backlog += _Now - _OwnerLast;
        else { _Backlog = _Period; _OwnerStarted = true; }
        _OwnerLast = _Now;
        if (_TokenStarted) _Balance += _Now - _TokenLast;
        else { _Balance = _Period; _TokenStarted = true; }
        _TokenLast = _Now;
        _Balance = std::min(_Balance, 30u * _Period);
    }
    bool TakePass() {
        // The owner's comparison is signed; the token comparison is unsigned.
        if ((_Backlog & 0x80000000u) || _Backlog < _Period || _Balance < _Period) return false;
        _Backlog -= _Period;
        _Balance -= _Period;
        return true;
    }
    float Fraction() const { return std::min(1.0f, float(_Backlog) / float(_Period)); }
    uint32_t Backlog() const { return _Backlog; }
    uint32_t TokenBalance() const { return _Balance; }
private:
    int _Raw = 760;
    uint32_t _Period = 42, _Now = 0, _OwnerLast = 0, _TokenLast = 0;
    uint32_t _Backlog = 0, _Balance = 0;
    bool _OwnerStarted = false, _TokenStarted = false;
    double _Fraction = 0;
};

}}}
