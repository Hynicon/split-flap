// Shared master <-> unit I2C protocol constants. See firmware/PROTOCOL.md.
#pragma once

#define UNIT_I2C_BASE 0x10     // unit address = UNIT_I2C_BASE + DIP value (0..15); avoids 0x00 general call
#define UNIT_MAX_COUNT 16      // DIP switch has 4 bits
#define UNIT_ANSWER_SIZE 1     // unit -> master: 1 byte "currently rotating"
#define CMD_REHOME 0xFE        // optional 3rd command byte: re-home before moving
#define FLAP_SPEED_MIN 1       // rpm
#define FLAP_SPEED_MAX 12      // rpm
