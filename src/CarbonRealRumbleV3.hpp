#pragma once

#ifdef GAME_CARBON

#include "CarbonRealRumbleV2.hpp"

namespace CarbonRealRumbleV3 {

// Carbon 1.4 executable addresses recovered from the supplied NFSC.exe decompilation.
// LocalPlayer::OnTask calls LocalPlayer::DoFFB here with ECX = concrete LocalPlayer base.
constexpr std::uintptr_t kDoFFBAddr     = 0x00762000;
constexpr std::uintptr_t kDoFFBCallSite = 0x00765196;
constexpr std::uintptr_t kLocalPlayerFFBOffset = 0x50;

typedef void(__thiscall* DoFFB_t)(void*);
static DoFFB_t OriginalDoFFB = reinterpret_cast<DoFFB_t>(kDoFFBAddr);
static bool gLoggedDirectAttach = false;
static bool gLoggedNoFeedback = false;

static CarbonRealRumbleV2::Feedback* GetPrimaryFeedback() {
  for (auto& state : CarbonRealRumbleV2::g_states) {
    if (state.iface && state.deviceIndex == 0) return state.iface;
  }
  return nullptr;
}

// __fastcall is the standard way to hook a no-argument __thiscall on x86:
// ECX carries `this`; EDX is an unused dummy argument.
static void __fastcall DoFFBHook(void* localPlayer, void*) {
  auto* feedback = GetPrimaryFeedback();

  if (localPlayer && feedback) {
    auto** slot = reinterpret_cast<CarbonRealRumbleV2::Feedback**>(
        reinterpret_cast<std::uintptr_t>(localPlayer) + kLocalPlayerFFBOffset);

    // Native Carbon leaves this null with XtendedInput, which makes DoFFB return
    // immediately. Attach our exact-ABI IFeedback object just before the original
    // DoFFB executes. Do not overwrite a non-null interface owned by another mod.
    if (*slot == nullptr) {
      *slot = feedback;
      if (!gLoggedDirectAttach) {
        CarbonRealRumbleV2::Log(
            "RealRumbleV3: attached LocalPlayer mFFB localPlayer=%p slot=%p feedback=%p",
            localPlayer, slot, feedback);
        gLoggedDirectAttach = true;
      }
    }
  } else if (!feedback && !gLoggedNoFeedback) {
    CarbonRealRumbleV2::Log("RealRumbleV3: DoFFB reached before controller feedback was created");
    gLoggedNoFeedback = true;
  }

  // Preserve all original Black Box gameplay calculations. Once mFFB is non-null,
  // Carbon itself produces tire slip/skid, road, RPM, NOS and shifting updates.
  OriginalDoFFB(localPlayer);
}

static void Install() {
  CarbonRealRumbleV2::Install();

  // Original bytes at 0x00765196 are a relative CALL to 0x00762000.
  // Replace only that call; surrounding LocalPlayer::OnTask logic stays untouched.
  injector::MakeCALL(kDoFFBCallSite, DoFFBHook, true);

  CarbonRealRumbleV2::Log(
      "RealRumbleV3: direct DoFFB attach installed callsite=0x%08X target=0x%08X mFFBOffset=0x%X",
      static_cast<unsigned int>(kDoFFBCallSite),
      static_cast<unsigned int>(kDoFFBAddr),
      static_cast<unsigned int>(kLocalPlayerFFBOffset));
}

}  // namespace CarbonRealRumbleV3

#endif  // GAME_CARBON
