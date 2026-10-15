#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "LuaBridge.h"

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved)
{
    (void)reserved;

    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(module);

    return TRUE;
}

extern "C" __declspec(dllexport) int luaopen_CallHorse(void* L)
{
    return bridge::RegisterLibrary(L) ? 1 : 0;
}
