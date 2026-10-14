#include "pch.h"

#include <Windows.h>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "HookUtils.h"
#include "log.h"

#include "AddressSet.h"
#include "BuddyWhistle.h"
#include "LuaApi.h"

namespace
{
    using SetCallPoint_t = void(__fastcall*)(void* manager, const void* position, float rotation);
    using CallWrapper_t  = std::uint8_t(__fastcall*)(void* manager, std::uint8_t buddyType, std::uint8_t force);

    constexpr std::uintptr_t kBuddyManagerGlobal = 0x142C28930ull;
    constexpr std::uintptr_t kSetCallPointAddr   = 0x140A47D20ull;
    constexpr std::uintptr_t kCallWrapperAddr    = 0x140A487B0ull;
    constexpr std::size_t    kControllerOffset   = 0x10;

    constexpr std::uint8_t kSetCallPointBytes[] =
    {
        0x48, 0x8B, 0x41, 0x10, 0x0F, 0x28, 0x02, 0x0F, 0x29, 0x80, 0xB0, 0x00, 0x00, 0x00
    };

    constexpr std::uint8_t kCallWrapperBytes[] =
    {
        0x48, 0x8B, 0x49, 0x10, 0x45, 0x0F, 0xB6, 0xC8, 0x41, 0xB0, 0x03
    };

    SetCallPoint_t g_SetCallPoint = nullptr;
    CallWrapper_t  g_CallWrapper  = nullptr;
    void**         g_ManagerSlot  = nullptr;
    bool           g_Resolved     = false;
    bool           g_Available    = false;

    bool BytesMatch(const void* address, const std::uint8_t* expected, std::size_t length)
    {
        return address != nullptr && std::memcmp(address, expected, length) == 0;
    }

    bool ResolveWhistle()
    {
        if (g_Resolved)
            return g_Available;

        g_Resolved = true;

        if (gGameBuild != AddressSetRuntime::GameBuild::En_1_0_15_4)
        {
            Log("[BuddyWhistle] unsupported build: %s\n", GetGameBuildName(gGameBuild));
            return false;
        }

        void* setCallPoint = ResolveGameAddress(kSetCallPointAddr);
        void* callWrapper  = ResolveGameAddress(kCallWrapperAddr);
        void* managerSlot  = ResolveGameAddress(kBuddyManagerGlobal);

        if (!setCallPoint || !callWrapper || !managerSlot)
        {
            Log("[BuddyWhistle] address resolution failed\n");
            return false;
        }

        if (!BytesMatch(setCallPoint, kSetCallPointBytes, sizeof(kSetCallPointBytes)))
        {
            Log("[BuddyWhistle] SetCallPoint signature mismatch at %p\n", setCallPoint);
            return false;
        }

        if (!BytesMatch(callWrapper, kCallWrapperBytes, sizeof(kCallWrapperBytes)))
        {
            Log("[BuddyWhistle] CallWrapper signature mismatch at %p\n", callWrapper);
            return false;
        }

        g_SetCallPoint = reinterpret_cast<SetCallPoint_t>(setCallPoint);
        g_CallWrapper  = reinterpret_cast<CallWrapper_t>(callWrapper);
        g_ManagerSlot  = reinterpret_cast<void**>(managerSlot);
        g_Available    = true;

        Log("[BuddyWhistle] ready: setCallPoint=%p callWrapper=%p managerSlot=%p\n",
            setCallPoint, callWrapper, managerSlot);
        return true;
    }

    bool WhistleBuddy(int buddyType, float x, float y, float z, float rotationRadians, bool force)
    {
        if (!ResolveWhistle())
            return false;

        void* manager = *g_ManagerSlot;
        if (!manager)
        {
            Log("[BuddyWhistle] buddy manager is null\n");
            return false;
        }

        void* controller = *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(manager) + kControllerOffset);
        if (!controller)
        {
            Log("[BuddyWhistle] buddy controller is null\n");
            return false;
        }

        alignas(16) float position[4] = { x, y, z, 0.0f };

        g_SetCallPoint(manager, position, rotationRadians);

        const std::uint8_t result = g_CallWrapper(
            manager,
            static_cast<std::uint8_t>(buddyType),
            static_cast<std::uint8_t>(force ? 1 : 0));

        Log("[BuddyWhistle] type=%d pos=(%.2f, %.2f, %.2f) rot=%.3f force=%d -> %d\n",
            buddyType, x, y, z, rotationRadians, force ? 1 : 0, static_cast<int>(result));

        return result != 0;
    }
}

int __cdecl l_WhistleBuddy(lua_State* L)
{
    if (!LuaIsNumber(L, 1) || !LuaIsNumber(L, 2) || !LuaIsNumber(L, 3) ||
        !LuaIsNumber(L, 4) || !LuaIsNumber(L, 5))
    {
        PushLuaBool(L, false);
        return 1;
    }

    const int   buddyType       = GetLuaInt(L, 1);
    const float x               = GetLuaNumber(L, 2);
    const float y               = GetLuaNumber(L, 3);
    const float z               = GetLuaNumber(L, 4);
    const float rotationRadians = GetLuaNumber(L, 5);
    const bool  force           = GetLuaTop(L) < 6 || GetLuaBool(L, 6);

    PushLuaBool(L, WhistleBuddy(buddyType, x, y, z, rotationRadians, force));
    return 1;
}

int __cdecl l_IsBuddyWhistleAvailable(lua_State* L)
{
    PushLuaBool(L, ResolveWhistle());
    return 1;
}
