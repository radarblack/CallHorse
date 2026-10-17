#include <cstddef>
#include <cstdint>

#include "BuddyWhistle.h"
#include "GameAddresses.h"
#include "Log.h"
#include "Spy.h"

namespace
{
    constexpr std::size_t kControllerOffset = 0x10;
    constexpr std::size_t kCommandStateOffset = 0x2E61;

    constexpr int kLimit61 = 7;
    constexpr int kLimit62 = 6;
    constexpr int kLimit63 = 7;
    constexpr int kLimit64 = 4;

    bool ControllerReady()
    {
        const game::BuddyApi& api = game::Buddy();

        void* manager = *api.managerSlot;
        if (!manager)
        {
            Log("buddy manager is null");
            return false;
        }

        void* controller = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(manager) + kControllerOffset);
        if (!controller)
        {
            Log("buddy controller is null");
            return false;
        }

        return true;
    }
}

namespace buddy
{
    bool Available()
    {
        return game::Resolve();
    }

    bool Whistle(int buddyType, float x, float y, float z, float rotationRadians, bool force)
    {
        if (buddyType < 0 || buddyType > 255)
        {
            Log("whistle rejected: buddy type %d is out of range", buddyType);
            return false;
        }

        if (!game::Resolve())
            return false;

        const game::BuddyApi& api = game::Buddy();

        void* manager = *api.managerSlot;
        if (!manager)
        {
            Log("whistle rejected: buddy manager is null");
            return false;
        }

        void* controller = *reinterpret_cast<void**>(static_cast<std::uint8_t*>(manager) + kControllerOffset);
        if (!controller)
        {
            Log("whistle rejected: buddy controller is null");
            return false;
        }

        alignas(16) const float position[4] = { x, y, z, 0.0f };

        spy::OwnCallScope ownCall;

        api.setCallPoint(manager, position, rotationRadians);

        const std::uint8_t result = api.callWrapper(
            manager,
            static_cast<std::uint8_t>(buddyType),
            static_cast<std::uint8_t>(force ? 1 : 0));

        Log("whistle type=%d pos=(%.2f, %.2f, %.2f) rot=%.3f force=%d result=%d",
            buddyType, x, y, z, rotationRadians, force ? 1 : 0, static_cast<int>(result));

        return result != 0;
    }

    bool GetHorseCommandState(std::uint8_t (&state)[4])
    {
        if (!game::Resolve())
            return false;

        const std::uint8_t* player = game::Buddy().playerState();
        if (!player)
            return false;

        for (std::size_t index = 0; index < 4; ++index)
            state[index] = player[kCommandStateOffset + index];

        return true;
    }

    bool SetHorseCommandState(int e61, int e62, int e63, int e64, bool trigger)
    {
        if (e61 < 0 || e61 >= kLimit61 || e62 < 0 || e62 >= kLimit62 ||
            e63 < 0 || e63 >= kLimit63 || e64 < 0 || e64 >= kLimit64)
        {
            Log("horse command rejected: %d,%d,%d,%d is out of range", e61, e62, e63, e64);
            return false;
        }

        if (!game::Resolve())
            return false;

        const game::BuddyApi& api = game::Buddy();

        if (!api.playerState())
        {
            Log("horse command rejected: player state is null");
            return false;
        }

        if (trigger && !ControllerReady())
            return false;

        spy::OwnCallScope ownCall;

        api.horseCommand(
            nullptr,
            static_cast<std::uint8_t>(e62),
            static_cast<std::uint8_t>(e61),
            static_cast<std::uint8_t>(e63),
            static_cast<std::uint8_t>(e64),
            static_cast<std::uint8_t>(trigger ? 1 : 0));

        Log("horse command %d,%d,%d,%d trigger=%d", e61, e62, e63, e64, trigger ? 1 : 0);
        return true;
    }
}
