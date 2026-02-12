/* secure-world/ta/cflat_ta.h */
#ifndef CFLAT_TA_H
#define CFLAT_TA_H

/* UUID: Generate with uuidgen */
#define TA_CFLAT_UUID \
    { 0xa1b2c3d4, 0x5678, 0x9abc, \
        { 0xde, 0xf0, 0x12, 0x34, 0x56, 0x78, 0x9a, 0xbc} }

/* Commands */
#define CMD_INIT                0
#define CMD_RECORD_NODE         1
#define CMD_LOOP_ENTER          2
#define CMD_LOOP_EXIT           3
#define CMD_LOOP_ITERATION      4  // NEW
#define CMD_CALL_ENTER          5  // Renumbered from 4
#define CMD_CALL_RETURN         6  // Renumbered from 5
#define CMD_FINALIZE            7  // Renumbered from 6
#define CMD_GET_AUTH            8  // Renumbered from 7
#define CMD_GET_LOG             9  // Renumbered from 8

/* Log Tags */
#define TAG_NODE          0x01
#define TAG_LOOP_ENTER    0x02
#define TAG_LOOP_EXIT     0x03
#define TAG_CALL          0x04
#define TAG_RETURN        0x05

#endif /* CFLAT_TA_H */
