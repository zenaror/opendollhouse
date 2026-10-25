#include "odh_simulation.h"

#include <stdint.h>

static int32_t ODH_SIM_CALL id(void* context, uint64_t token, int16_t* object_id) {
    (void)context;
    *object_id = (int16_t)token;
    return 0;
}
static void ODH_SIM_CALL retain(void* context, uint64_t token) {
    (void)context; (void)token;
}
static void ODH_SIM_CALL release(void* context, uint64_t token) {
    (void)context; (void)token;
}

int main(void) {
    odh_wake_queue* queue = 0;
    if (odh_wake_abi_version() != 1 ||
        odh_wake_create_v1(0, id, retain, release, 4, 8, 65536, &queue) != ODH_WAKE_OK ||
        !queue) return 1;
    if (odh_wake_schedule_v1(queue, 10, 5) != ODH_WAKE_OK) return 2;
    uint32_t found = 0;
    if (odh_wake_select_current_v1(queue, 5, &found) != ODH_WAKE_OK || found != 1) return 3;
    uint64_t token = 0;
    if (odh_wake_current_at_v1(queue, 0, &token) != ODH_WAKE_OK || token != 10) return 4;
    return odh_wake_destroy_v1(queue) == ODH_WAKE_OK ? 0 : 5;
}
