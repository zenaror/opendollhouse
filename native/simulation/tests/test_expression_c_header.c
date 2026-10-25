#include "odh_expression.h"

static int32_t ODH_SIM_CALL read_value(void* context, uint8_t scope, int16_t index, int32_t* value) {
    (void)context; (void)index;
    *value = scope == 1 ? 4 : 9;
    return 0;
}
static int32_t ODH_SIM_CALL write_value(void* context, uint8_t scope, int16_t index, int32_t value, uint32_t* accepted) {
    (void)scope; (void)index;
    *(int32_t*)context = value; *accepted = 1;
    return 0;
}
int main(void) {
    const uint8_t operand[8] = {0,0,1,0,0,3,1,2};
    int32_t written = 0;
    uint32_t result = 99;
    return !(odh_expression_abi_version() == 1 &&
        odh_expression_execute_ts1_v1(operand,8,&written,read_value,write_value,0,&result) == ODH_EXPRESSION_OK &&
        written == 13 && result == 1);
}
