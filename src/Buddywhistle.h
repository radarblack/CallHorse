#pragma once

#include <cstdint>

namespace buddy
{
    struct BuddyCommand
    {
        std::uint32_t kind     = 0;
        std::uint8_t  type     = 0;
        float         x        = 0.0f;
        float         y        = 0.0f;
        float         z        = 0.0f;
        float         rotation = 0.0f;
        std::uint8_t  a18      = 0;
        std::uint8_t  a1c      = 0;
        std::uint8_t  a20      = 0;
        std::uint8_t  a24      = 0;
        std::uint8_t  f64      = 0;
    };

    bool Available();
    bool Whistle(int buddyType, float x, float y, float z, float rotationRadians, bool force);
    bool GetHorseCommandState(std::uint8_t (&state)[4]);
    bool SetHorseCommandState(int e61, int e62, int e63, int e64, bool trigger);
    bool RunBuddyCommand(const BuddyCommand& command, int& result);
}
