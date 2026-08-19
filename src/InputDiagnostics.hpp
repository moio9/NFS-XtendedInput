#pragma once
#ifdef GAME_CARBON
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>

namespace InputDiagnostics {

static void Log(const char* fmt, ...) {
  FILE* f = std::fopen("NFS_XtendedInput_InputDiagnostics.log", "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

static unsigned CountXInputMappings() {
  unsigned n = 0;
  for (unsigned i = 0; i < MAX_ACTIONID; ++i)
    if (XInputBindings_PRIMARY[i] || XInputBindings_SECONDARY[i]) ++n;
  return n;
}

static unsigned CountKeyboardMappings() {
  unsigned n = 0;
  for (unsigned i = 0; i < MAX_ACTIONID; ++i)
    if (VKeyBindings_PRIMARY[i] || VKeyBindings_SECONDARY[i]) ++n;
  return n;
}

static unsigned CountActiveCurrValues() {
  unsigned n = 0;
  for (unsigned p = 0; p < 2; ++p)
    for (unsigned i = 0; i < MAX_ACTIONID; ++i)
      if (CurrValues[p][i] != 0.0f) ++n;
  return n;
}

static void LogMappingFiles() {
  Log("InputDiag: cwd default-map exists=%s path=%s",
      PathFileExistsA(defMappingName) ? "yes" : "no", defMappingName);
  Log("InputDiag: config exists=%s path=%s",
      PathFileExistsA(cfgIniName) ? "yes" : "no", cfgIniName);
  Log("InputDiag: profile='%s'", gProfileName.c_str());

  if (!gProfileName.empty()) {
    char userMap[MAX_PATH]{};
    std::snprintf(userMap, sizeof(userMap), "%s\\%s\\%s", userMappingDir, gProfileName.c_str(), userMappingName);
    Log("InputDiag: user-map exists=%s path=%s", PathFileExistsA(userMap) ? "yes" : "no", userMap);
  }
}

static DWORD WINAPI Worker(LPVOID) {
  std::remove("NFS_XtendedInput_InputDiagnostics.log");
  Sleep(1200);

  Log("InputDiag: started");
  LogMappingFiles();
  Log("InputDiag: mappings xinput=%u keyboard=%u globalPolling=%s keyboardMode=%u",
      CountXInputMappings(), CountKeyboardMappings(), bGlobalDoPolling ? "yes" : "no", KeyboardReadingMode);

  DWORD lastPacket = 0xFFFFFFFFu;
  unsigned lastActive = ~0u;
  bool lastGlobalPolling = !bGlobalDoPolling;
  int lastControlled = -1;
  DWORD lastPeriodic = GetTickCount();

  for (;;) {
    XINPUT_STATE xs{};
    DWORD xr = XInputGetState(0, &xs);

    if (xr == ERROR_SUCCESS && xs.dwPacketNumber != lastPacket) {
      Log("InputDiag: raw XInput packet=%lu buttons=0x%04X LT=%u RT=%u LX=%d LY=%d RX=%d RY=%d",
          static_cast<unsigned long>(xs.dwPacketNumber), xs.Gamepad.wButtons,
          xs.Gamepad.bLeftTrigger, xs.Gamepad.bRightTrigger,
          xs.Gamepad.sThumbLX, xs.Gamepad.sThumbLY, xs.Gamepad.sThumbRX, xs.Gamepad.sThumbRY);
      lastPacket = xs.dwPacketNumber;
    }

    unsigned active = CountActiveCurrValues();
    if (active != lastActive) {
      Log("InputDiag: CurrValues active=%u", active);
      lastActive = active;
    }

    if (lastGlobalPolling != bGlobalDoPolling) {
      Log("InputDiag: bGlobalDoPolling=%s", bGlobalDoPolling ? "true" : "false");
      lastGlobalPolling = bGlobalDoPolling;
    }

    if (lastControlled != static_cast<int>(LastControlledDevice)) {
      Log("InputDiag: LastControlledDevice=%u", LastControlledDevice);
      lastControlled = static_cast<int>(LastControlledDevice);
    }

    // Capture useful keyboard activity independently from XtendedInput mappings.
    static const int keys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_ESCAPE, VK_SPACE, 'W', 'A', 'S', 'D'};
    for (int vk : keys) {
      if (GetAsyncKeyState(vk) & 0x8000) {
        Log("InputDiag: raw keyboard vk=0x%02X down", vk);
        Sleep(120); // debounce log spam while still making polling visible
        break;
      }
    }

    DWORD now = GetTickCount();
    if (now - lastPeriodic >= 5000) {
      Log("InputDiag: heartbeat rawXInput=%lu activeCurr=%u mappingsX=%u mappingsKB=%u poll=%s",
          static_cast<unsigned long>(xr), active, CountXInputMappings(), CountKeyboardMappings(),
          bGlobalDoPolling ? "yes" : "no");
      lastPeriodic = now;
    }

    Sleep(25);
  }
}

static void Start() {
  HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
  if (t) CloseHandle(t);
}

} // namespace InputDiagnostics
#endif
