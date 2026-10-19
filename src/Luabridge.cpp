#include "BuddyWhistle.h"
#include "GameAddresses.h"
#include "Log.h"
#include "LuaBridge.h"
#include "Spy.h"

namespace
{
    constexpr const char* kLibraryName = "CallHorseNative";
    constexpr int         kLuaTypeNil  = 0;

    struct LuaReg
    {
        const char* name;
        int       (*function)(void* L);
    };

    int WhistleBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        for (int index = 1; index <= 5; ++index)
        {
            if (!lua.isnumber(L, index))
            {
                lua.pushboolean(L, 0);
                return 1;
            }
        }

        const double typeValue = lua.tonumber(L, 1);
        if (!(typeValue >= 0.0 && typeValue <= 255.0))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        const float x               = static_cast<float>(lua.tonumber(L, 2));
        const float y               = static_cast<float>(lua.tonumber(L, 3));
        const float z               = static_cast<float>(lua.tonumber(L, 4));
        const float rotationRadians = static_cast<float>(lua.tonumber(L, 5));
        const bool  force           = lua.type(L, 6) <= kLuaTypeNil || lua.toboolean(L, 6) != 0;

        const bool accepted = buddy::Whistle(static_cast<int>(typeValue), x, y, z, rotationRadians, force);
        lua.pushboolean(L, accepted ? 1 : 0);
        return 1;
    }

    bool InRange(double value)
    {
        return value >= 0.0 && value <= 255.0;
    }

    int GetHorseCommandStateBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        std::uint8_t state[4] = {};
        if (!buddy::GetHorseCommandState(state))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        for (int index = 0; index < 4; ++index)
            lua.pushnumber(L, static_cast<double>(state[index]));

        return 4;
    }

    int SetHorseCommandStateBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        double values[4] = {};
        for (int index = 0; index < 4; ++index)
        {
            if (!lua.isnumber(L, index + 1))
            {
                lua.pushboolean(L, 0);
                return 1;
            }

            values[index] = lua.tonumber(L, index + 1);
            if (!InRange(values[index]))
            {
                lua.pushboolean(L, 0);
                return 1;
            }
        }

        const bool trigger = lua.type(L, 5) <= kLuaTypeNil || lua.toboolean(L, 5) != 0;

        const bool done = buddy::SetHorseCommandState(
            static_cast<int>(values[0]),
            static_cast<int>(values[1]),
            static_cast<int>(values[2]),
            static_cast<int>(values[3]),
            trigger);

        lua.pushboolean(L, done ? 1 : 0);
        return 1;
    }

    int EnableSpyBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        const bool enabled = lua.type(L, 1) <= kLuaTypeNil || lua.toboolean(L, 1) != 0;
        lua.pushboolean(L, spy::Enable(enabled) ? 1 : 0);
        return 1;
    }

    int GetLastGameHorseCommandBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        std::uint32_t sequence = 0;
        std::uint8_t command[4] = {};
        if (!spy::GetLastGameHorseCommand(sequence, command))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        lua.pushnumber(L, static_cast<double>(sequence));
        for (int index = 0; index < 4; ++index)
            lua.pushnumber(L, static_cast<double>(command[index]));

        return 5;
    }

    int PushBuddyRecord(void* L, const spy::BuddyCommandRecord& record)
    {
        const game::LuaApi& lua = game::Lua();

        lua.pushnumber(L, static_cast<double>(record.sequence));
        lua.pushnumber(L, static_cast<double>(record.kind));
        lua.pushnumber(L, static_cast<double>(record.type));
        lua.pushnumber(L, static_cast<double>(record.x));
        lua.pushnumber(L, static_cast<double>(record.y));
        lua.pushnumber(L, static_cast<double>(record.z));
        lua.pushnumber(L, static_cast<double>(record.rotation));
        lua.pushnumber(L, static_cast<double>(record.a18));
        lua.pushnumber(L, static_cast<double>(record.a1c));
        lua.pushnumber(L, static_cast<double>(record.a20));
        lua.pushnumber(L, static_cast<double>(record.a24));
        lua.pushnumber(L, static_cast<double>(record.f64));
        lua.pushnumber(L, static_cast<double>(record.result));
        return 13;
    }

    int GetLastGameBuddyCommandBinding(void* L)
    {
        spy::BuddyCommandRecord record;
        if (!spy::GetLastGameBuddyCommand(record))
        {
            game::Lua().pushboolean(L, 0);
            return 1;
        }

        return PushBuddyRecord(L, record);
    }

    int GetGameBuddyCommandBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        if (!lua.isnumber(L, 1))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        const double sequence = lua.tonumber(L, 1);
        spy::BuddyCommandRecord record;
        if (!(sequence >= 1.0 && sequence <= 4294967295.0) ||
            !spy::GetGameBuddyCommand(static_cast<std::uint32_t>(sequence), record))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        return PushBuddyRecord(L, record);
    }

    int RunBuddyCommandBinding(void* L)
    {
        const game::LuaApi& lua = game::Lua();

        double values[11] = {};
        for (int index = 0; index < 11; ++index)
        {
            if (!lua.isnumber(L, index + 1))
            {
                lua.pushboolean(L, 0);
                return 1;
            }

            values[index] = lua.tonumber(L, index + 1);
        }

        if (!(values[0] >= 0.0 && values[0] <= 5.0) || !InRange(values[1]) ||
            !InRange(values[6]) || !InRange(values[7]) || !InRange(values[8]) ||
            !InRange(values[9]) || !InRange(values[10]))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        buddy::BuddyCommand command;
        command.kind     = static_cast<std::uint32_t>(values[0]);
        command.type     = static_cast<std::uint8_t>(values[1]);
        command.x        = static_cast<float>(values[2]);
        command.y        = static_cast<float>(values[3]);
        command.z        = static_cast<float>(values[4]);
        command.rotation = static_cast<float>(values[5]);
        command.a18      = static_cast<std::uint8_t>(values[6]);
        command.a1c      = static_cast<std::uint8_t>(values[7]);
        command.a20      = static_cast<std::uint8_t>(values[8]);
        command.a24      = static_cast<std::uint8_t>(values[9]);
        command.f64      = static_cast<std::uint8_t>(values[10]);

        int result = 0;
        if (!buddy::RunBuddyCommand(command, result))
        {
            lua.pushboolean(L, 0);
            return 1;
        }

        lua.pushnumber(L, static_cast<double>(result));
        return 1;
    }

    int IsAvailableBinding(void* L)
    {
        game::Lua().pushboolean(L, buddy::Available() ? 1 : 0);
        return 1;
    }

    const LuaReg kLibrary[] =
    {
        { "Whistle",     WhistleBinding },
        { "IsAvailable", IsAvailableBinding },
        { "GetHorseCommandState", GetHorseCommandStateBinding },
        { "SetHorseCommandState", SetHorseCommandStateBinding },
        { "EnableSpy", EnableSpyBinding },
        { "GetLastGameHorseCommand", GetLastGameHorseCommandBinding },
        { "GetLastGameBuddyCommand", GetLastGameBuddyCommandBinding },
        { "GetGameBuddyCommand", GetGameBuddyCommandBinding },
        { "RunBuddyCommand", RunBuddyCommandBinding },
        { nullptr,       nullptr }
    };
}

namespace bridge
{
    bool RegisterLibrary(void* L)
    {
        if (!L)
            return false;

        if (!game::Resolve())
            return false;

        game::Lua().registerLibrary(L, kLibraryName, kLibrary);
        Log("registered Lua library %s", kLibraryName);
        return true;
    }
}
