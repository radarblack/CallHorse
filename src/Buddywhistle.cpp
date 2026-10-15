#include <cstddef>
#include <cstdint>

#include "BuddyWhistle.h"
#include "GameAddresses.h"
#include "Log.h"

namespace
{
    constexpr std::size_t kControllerOffset = 0x10;
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

        api.setCallPoint(manager, position, rotationRadians);

        const std::uint8_t result = api.callWrapper(
            manager,
            static_cast<std::uint8_t>(buddyType),
            static_cast<std::uint8_t>(force ? 1 : 0));

        Log("whistle type=%d pos=(%.2f, %.2f, %.2f) rot=%.3f force=%d result=%d",
            buddyType, x, y, z, rotationRadians, force ? 1 : 0, static_cast<int>(result));

        return result != 0;
    }
}
