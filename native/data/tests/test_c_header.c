#include "odh_data.h"

#include <stdint.h>

int main(void) {
    const uint8_t empty[] = {0, 0x42};
    uint32_t count = 99, flags = 99, remaining = 99;
    return odh_data_abi_version() == 1 &&
           odh_bcon_decode_v1(empty, 2, 0, 0, &count, &flags, &remaining) == ODH_DATA_OK &&
           count == 0 && flags == 0x42 && remaining == 0 ? 0 : 1;
}
