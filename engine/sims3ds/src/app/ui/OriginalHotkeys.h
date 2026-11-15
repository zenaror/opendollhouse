/* SPDX-License-Identifier: MPL-2.0 */
#pragma once
namespace simapp::original {
// Native states: cutaway=0, down=1, up=2. Home/End follow the displayed
// wall order and stop at the limit, rather than wrapping the storage enum.
inline int StepOriginalWalls(int state, int direction) {
    const int order[3] = {1, 0, 2};
    int i = state == 1 ? 0 : state == 2 ? 2 : 1;
    i += direction;
    if (i < 0) i = 0;
    if (i > 2) i = 2;
    return order[i];
}
}
