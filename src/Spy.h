#pragma once

#include <cstdint>

namespace spy
{
    bool Enable(bool enabled);
    bool GetLastGameHorseCommand(std::uint32_t& sequence, std::uint8_t (&command)[4]);

    void BeginOwnCall();
    void EndOwnCall();

    class OwnCallScope
    {
    public:
        OwnCallScope() { BeginOwnCall(); }
        ~OwnCallScope() { EndOwnCall(); }
        OwnCallScope(const OwnCallScope&) = delete;
        OwnCallScope& operator=(const OwnCallScope&) = delete;
    };
}
