#pragma once

#include <cstddef>

namespace inlinehook
{
    constexpr std::size_t kJumpSize = 14;
    constexpr std::size_t kMaxStolen = 32;
    constexpr std::size_t kRipLoadSize = 7;

    struct Hook
    {
        void*         target     = nullptr;
        void*         trampoline = nullptr;
        std::size_t   stolen     = 0;
        unsigned char saved[kMaxStolen] = {};
    };

    bool Install(Hook& hook, void* target, std::size_t stolen, void* detour, void** original = nullptr);
    bool InstallWithRipLoad(Hook& hook, void* target, std::size_t ripLoadOffset, void* detour, void** original = nullptr);
    void Remove(Hook& hook);
}
