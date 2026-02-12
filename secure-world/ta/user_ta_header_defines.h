#ifndef USER_TA_HEADER_DEFINES_H
#define USER_TA_HEADER_DEFINES_H

#include <cflat_ta.h>

#define TA_UUID TA_CFLAT_UUID

#define TA_FLAGS (TA_FLAG_EXEC_DDR | TA_FLAG_SINGLE_INSTANCE)
#define TA_STACK_SIZE (64 * 1024)
#define TA_DATA_SIZE  (128 * 1024)

#define TA_CURRENT_TA_EXT_PROPERTIES \
    { "gp.ta.description", USER_TA_PROP_TYPE_STRING, "C-FLAT Attestation TA" }, \
    { "gp.ta.version", USER_TA_PROP_TYPE_U32, &(const uint32_t){0x0100} }

#endif
