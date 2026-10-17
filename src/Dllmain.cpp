#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Log.h"
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
    Log("luaopen_CallHorse called");

    const bool registered = bridge::RegisterLibrary(L);
    Log("luaopen_CallHorse %s", registered ? "registered the library" : "failed, native functions are unavailable");

    return registered ? 1 : 0;
}
