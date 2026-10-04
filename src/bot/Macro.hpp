#pragma once
#include <Geode/Geode.hpp>
#include <cstdint>
#include <string>
#include <vector>

namespace ivey {

    // .ivey layout (little-endian). Accuracy is the very first field.
    //   u16 accuracy | u8 flags | u8 version | "IVEY" | i32 levelID | u32 count
    //   entry: u32 frame | u8 input | u8 state | u8 player
    //   v2 (read only): u32 count, then 17 byte checks
    //   v3, when flags bit 1 is set: one track per player (P1, then P2)
    //     varint count, then per check:
    //       first: varint frame | zigzag x | zigzag y        (x, y in 1/32 units)
    //       next:  varint (gap << 1 | zero), and if zero is 0: zigzag dx | zigzag dy
    //     gap = frames since the last check - 1, dx/dy = miss against the straight-line guess
    //     zero = 1 means the guess was exactly right, nothing else is stored
    constexpr uint8_t  FORMAT_VERSION = 3;
    constexpr size_t   HEADER_SIZE    = 16;
    constexpr size_t   ENTRY_SIZE     = 7;
    constexpr size_t   CHECK_SIZE     = 17;

    struct Entry {
        uint32_t frame  = 0;
        uint8_t  input  = 1; // 1 jump, 2 left, 3 right
        uint8_t  state  = 0; // 1 press, 0 release
        uint8_t  player = 0; // 0 = P1, 1 = P2
    };

    // Where the player was at a frame. Used to pull a replay back on track.
    struct Check {
        uint32_t frame  = 0;
        uint8_t  player = 0;
        float    x      = 0.f;
        float    y      = 0.f;
        float    yVel   = 0.f;
    };

    struct Macro {
        uint16_t accuracy = 240; // steps per second
        uint8_t  flags    = 1;   // bit 0 = frame accurate, bit 1 = has checks (set on save),
                                 // bit 2 = frames are the game's own step count (macros from other bots)
        int32_t  levelID  = 0;
        std::string levelName; // kept in memory only, the file name carries it
        std::vector<Entry> entries;
        std::vector<Check> checks;

        std::vector<uint8_t> encode() const;
        static geode::Result<Macro> decode(std::vector<uint8_t> const& data);

        bool lastState(uint8_t player, uint8_t input) const;
    };
}
