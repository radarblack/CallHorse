#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <cstring>

#include "GameAddresses.h"
#include "Log.h"

namespace
{
    constexpr std::uintptr_t kPreferredBase = 0x140000000ull;

    constexpr std::uintptr_t kLuaIsNumber      = 0x141A11410ull;
    constexpr std::uintptr_t kLuaToNumber      = 0x141A121F0ull;
    constexpr std::uintptr_t kLuaToBoolean     = 0x141A120C0ull;
    constexpr std::uintptr_t kLuaPushBoolean   = 0x141A11750ull;
    constexpr std::uintptr_t kLuaType          = 0x141A12300ull;
    constexpr std::uintptr_t kRegisterLibrary  = 0x14006B8C0ull;
    constexpr std::uintptr_t kSetCallPoint     = 0x140A47D20ull;
    constexpr std::uintptr_t kCallWrapper      = 0x140A487B0ull;
    constexpr std::uintptr_t kBuddyManagerSlot = 0x142C28930ull;

    constexpr std::uint8_t kLuaIsNumberBytes[]     = { 0x48, 0x83, 0xEC, 0x38, 0xE8, 0x87, 0x22, 0x00, 0x00, 0x83, 0x78, 0x08, 0x03, 0x74, 0x17, 0x48 };
    constexpr std::uint8_t kLuaToNumberBytes[]     = { 0x48, 0x83, 0xEC, 0x38, 0xE8, 0xA7, 0x14, 0x00, 0x00, 0x83, 0x78, 0x08, 0x03, 0x74, 0x1A, 0x48 };
    constexpr std::uint8_t kLuaToBooleanBytes[]    = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0xD7, 0x15, 0x00, 0x00, 0x8B, 0x48, 0x08, 0x85, 0xC9, 0x74, 0x14 };
    constexpr std::uint8_t kLuaPushBooleanBytes[]  = { 0x4C, 0x8B, 0x41, 0x10, 0x33, 0xC0, 0x85, 0xD2, 0x0F, 0x95, 0xC0, 0x41, 0xC7, 0x40, 0x08, 0x01 };
    constexpr std::uint8_t kLuaTypeBytes[]         = { 0x48, 0x83, 0xEC, 0x28, 0xE8, 0x97, 0x13, 0x00, 0x00, 0x48, 0x8D, 0x0D, 0x80, 0x3A, 0xAA, 0x00 };
    constexpr std::uint8_t kRegisterLibraryBytes[] = { 0x40, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0xC7, 0x44, 0x24, 0x20, 0xFE, 0xFF, 0xFF, 0xFF, 0x48 };
    constexpr std::uint8_t kSetCallPointBytes[]    = { 0x48, 0x8B, 0x41, 0x10, 0x0F, 0x28, 0x02, 0x0F, 0x29, 0x80, 0xB0, 0x00, 0x00, 0x00, 0xF3, 0x0F };
    constexpr std::uint8_t kCallWrapperBytes[]     = { 0x48, 0x8B, 0x49, 0x10, 0x45, 0x0F, 0xB6, 0xC8, 0x41, 0xB0, 0x03, 0x45, 0x84, 0xC9, 0x74, 0x18 };

    struct Target
    {
        const char*         name;
        std::uintptr_t      address;
        const std::uint8_t* bytes;
        std::size_t         length;
    };

    constexpr Target kTargets[] =
    {
        { "lua_isnumber",   kLuaIsNumber,     kLuaIsNumberBytes,     sizeof(kLuaIsNumberBytes) },
        { "lua_tonumber",   kLuaToNumber,     kLuaToNumberBytes,     sizeof(kLuaToNumberBytes) },
        { "lua_toboolean",  kLuaToBoolean,    kLuaToBooleanBytes,    sizeof(kLuaToBooleanBytes) },
        { "lua_pushboolean", kLuaPushBoolean, kLuaPushBooleanBytes,  sizeof(kLuaPushBooleanBytes) },
        { "lua_type",       kLuaType,         kLuaTypeBytes,         sizeof(kLuaTypeBytes) },
        { "registerLibrary", kRegisterLibrary, kRegisterLibraryBytes, sizeof(kRegisterLibraryBytes) },
        { "setCallPoint",   kSetCallPoint,    kSetCallPointBytes,    sizeof(kSetCallPointBytes) },
        { "callWrapper",    kCallWrapper,     kCallWrapperBytes,     sizeof(kCallWrapperBytes) },
    };

    game::LuaApi   g_lua;
    game::BuddyApi g_buddy;
    bool           g_resolved = false;
    bool           g_ok       = false;

    std::uintptr_t ExeBase()
    {
        return reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));
    }

    void* Rebase(std::uintptr_t address)
    {
        return reinterpret_cast<void*>(ExeBase() + (address - kPreferredBase));
    }

    bool IsReadable(const void* address, std::size_t length)
    {
        MEMORY_BASIC_INFORMATION info;
        if (VirtualQuery(address, &info, sizeof(info)) == 0)
            return false;

        if (info.State != MEM_COMMIT)
            return false;

        if (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
            return false;

        const std::uintptr_t end       = reinterpret_cast<std::uintptr_t>(address) + length;
        const std::uintptr_t regionEnd = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        return end <= regionEnd;
    }

    bool Matches(const Target& target)
    {
        const void* address = Rebase(target.address);

        if (!IsReadable(address, target.length))
        {
            Log("%s: address %p is not readable", target.name, address);
            return false;
        }

        if (std::memcmp(address, target.bytes, target.length) != 0)
        {
            Log("%s: signature mismatch at %p", target.name, address);
            return false;
        }

        return true;
    }

    template <typename T>
    T Function(std::uintptr_t address)
    {
        return reinterpret_cast<T>(Rebase(address));
    }
}

namespace game
{
    bool Resolve()
    {
        if (g_resolved)
            return g_ok;

        g_resolved = true;

        for (const Target& target : kTargets)
        {
            if (!Matches(target))
            {
                Log("unsupported game build, native functions disabled");
                return false;
            }
        }

        void** slot = static_cast<void**>(Rebase(kBuddyManagerSlot));
        if (!IsReadable(slot, sizeof(void*)))
        {
            Log("buddy manager slot %p is not readable", static_cast<void*>(slot));
            return false;
        }

        g_lua.isnumber        = Function<decltype(g_lua.isnumber)>(kLuaIsNumber);
        g_lua.tonumber        = Function<decltype(g_lua.tonumber)>(kLuaToNumber);
        g_lua.toboolean       = Function<decltype(g_lua.toboolean)>(kLuaToBoolean);
        g_lua.pushboolean     = Function<decltype(g_lua.pushboolean)>(kLuaPushBoolean);
        g_lua.type            = Function<decltype(g_lua.type)>(kLuaType);
        g_lua.registerLibrary = Function<decltype(g_lua.registerLibrary)>(kRegisterLibrary);

        g_buddy.setCallPoint = Function<decltype(g_buddy.setCallPoint)>(kSetCallPoint);
        g_buddy.callWrapper  = Function<decltype(g_buddy.callWrapper)>(kCallWrapper);
        g_buddy.managerSlot  = slot;

        g_ok = true;
        Log("game functions resolved, exe base %p", reinterpret_cast<void*>(ExeBase()));
        return true;
    }

    const LuaApi& Lua()
    {
        return g_lua;
    }

    const BuddyApi& Buddy()
    {
        return g_buddy;
    }
}
