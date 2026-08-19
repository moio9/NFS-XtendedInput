/*
// clang-format off
// 
//    MIT License
//    Need for Speed (Black Box, MW & newer) - Xtended Input plugin
//    Bringing native XInput to NFS
//    
//    Copyright (c) 2022–2023 Lovro Plese (Xan/Tenjoin)
//    Copyright (c) 2023 Berkay Yigit <mail@berkay.link>
//
// clang-format on
*/

#include "stdafx.h"
#include "../Main.hpp"
#ifdef GAME_CARBON
#include "../DInputFFBOnly.hpp"
#endif

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
#ifdef GAME_WORLD
    uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    MainBase       = base - 0x400000;
#endif

#ifdef GAME_CARBON
    // Save Carbon's native DirectInput initialization call before XtendedInput
    // NOPs it. We restore it after XtendedInput has installed its own hooks.
    DInputFFBOnly::CaptureNativeDInputInitBytes();
#endif

    Init();

#ifdef GAME_CARBON
    // Keep native DirectInput alive for force feedback, but neutralize gamepad
    // state reads so gameplay input still comes only from XtendedInput/XInput.
    DInputFFBOnly::ActivateAfterXtendedInit();
#endif
  }
  return TRUE;
}
