// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstddef>
#include <utility>
#include <vector>

namespace FSO { namespace SimAntics { namespace Primitives { namespace TS1Autonomy {
// Complete Collection PC uses the same tri-state quicksort for action scores
// and motive IDs. Its small-range maximum swaps and partitioning reorder ties.
// A tolerant comparator is not a strict weak ordering: do not pass it to the
// standard C++ sorting algorithms. Reconstructed from DVD 0063560a..006357ab.
template<class T, class Compare>
void SortAutonomyRecords(std::vector<T>& a, Compare compare) {
    if (a.size() < 2) return;
    using I = std::ptrdiff_t;
    std::vector<std::pair<I,I>> pending;
    I lo=0, hi=I(a.size())-1;
    for (;;) {
        if (hi-lo+1 <= 8) {
            // 0x63575e: first maximum wins; swapping it to hi can move ties.
            while (hi > lo) {
                I maximum=lo;
                for (I p=lo+1; p<=hi; ++p)
                    if (compare(a[p],a[maximum]) > 0) maximum=p;
                std::swap(a[maximum],a[hi]);
                --hi;
            }
        } else {
            // 0x635689: pivot is floor(count/2), swapped to first.
            I mid=lo+(hi-lo+1)/2;
            std::swap(a[mid],a[lo]);
            I left=lo, right=hi+1;
            for (;;) {
                do { ++left; } while(left<=hi && compare(a[left],a[lo])<=0);
                do { --right; } while(right>lo && compare(a[right],a[lo])>=0);
                if(right<left) break;
                std::swap(a[left],a[right]);
            }
            std::swap(a[lo],a[right]);
            // 0x6356ef compares byte lengths (right-lo)*width-1 and (hi-left)*width.
            // Equivalent for positive widths: (right-lo) <= (hi-left).
            // Process smaller interval next, push larger for the explicit stack.
            if(right-lo > hi-left) {
                if(lo+1<right) pending.emplace_back(lo,right-1);
                if(left<hi) { lo=left; continue; }
            } else {
                if(left<hi) pending.emplace_back(left,hi);
                if(lo+1<right) { hi=right-1; continue; }
            }
        }
        if(pending.empty()) return;
        lo=pending.back().first; hi=pending.back().second; pending.pop_back();
    }
}

}}}}
