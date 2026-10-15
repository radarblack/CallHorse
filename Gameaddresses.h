#pragma once

#include <cstdint>

namespace game
{
    struct LuaApi
    {
        int    (*isnumber)(void* L, int index)                                  = nullptr;
        double (*tonumber)(void* L, int index)                                  = nullptr;
        int    (*toboolean)(void* L, int index)                                 = nullptr;
        void   (*pushboolean)(void* L, int value)                               = nullptr;
        int    (*type)(void* L, int index)                                      = nullptr;
        void   (*registerLibrary)(void* L, const char* name, const void* funcs) = nullptr;
    };

    struct BuddyApi
    {
        void         (*setCallPoint)(void* manager, const void* position, float rotation)                  = nullptr;
        std::uint8_t (*callWrapper)(void* manager, std::uint8_t buddyType, std::uint8_t force)             = nullptr;
        void**         managerSlot                                                                         = nullptr;
    };

    bool Resolve();
    const LuaApi& Lua();
    const BuddyApi& Buddy();
}
