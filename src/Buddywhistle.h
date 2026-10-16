#pragma once

#include <cstdint>

namespace buddy
{
    bool Available();
    bool Whistle(int buddyType, float x, float y, float z, float rotationRadians, bool force);
    bool GetHorseCommandState(std::uint8_t (&state)[4]);
    bool SetHorseCommandState(int e61, int e62, int e63, int e64, bool trigger);
}
