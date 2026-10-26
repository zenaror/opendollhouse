#include "odh_thread_state.h"
int main(void) {
    odh_thread_state* state = 0;
    odh_register_bank* bank = 0;
    int32_t value = 0;
    if (odh_state_abi_version() != 1 || odh_state_create_v1(1, &state)) return 1;
    if (odh_state_get_bank_v1(state, ODH_BANK_TEMP16, &bank)) return 2;
    if (odh_bank_write_v1(bank, 19, 65535) || odh_bank_read_v1(bank, 19, &value) || value != -1) return 3;
    if (odh_state_release_v1(state) || odh_bank_read_v1(bank, 19, &value) || value != -1) return 4;
    return odh_bank_release_v1(bank);
}
