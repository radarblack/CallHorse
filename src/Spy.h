#pragma once

#include <cstdint>

namespace spy
{
    struct BuddyCommandRecord
    {
        std::uint32_t sequence = 0;
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
        std::int32_t  result   = 0;
    };

    bool Enable(bool enabled);
    bool GetLastGameHorseCommand(std::uint32_t& sequence, std::uint8_t (&command)[4]);
    bool GetLastGameBuddyCommand(BuddyCommandRecord& record);
    bool GetGameBuddyCommand(std::uint32_t sequence, BuddyCommandRecord& record);

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
