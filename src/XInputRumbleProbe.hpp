#pragma once
#ifdef GAME_CARBON
#include <windows.h>
#include <cstdio>

namespace XInputRumbleProbe {

static void Log(const char* fmt, ...) {
  FILE* f = std::fopen("NFS_XtendedInput_XInputRumbleProbe.log", "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

static DWORD WINAPI Worker(LPVOID) {
  std::remove("NFS_XtendedInput_XInputRumbleProbe.log");
  Log("RumbleProbe: worker started");

  // Let the game and the XInput proxy finish loading before probing.
  Sleep(1500);

  for (int attempt = 0; attempt < 50; ++attempt) {
    XINPUT_STATE state{};
    DWORD getResult = XInputGetState(0, &state);
    if (getResult == ERROR_SUCCESS) {
      Log("RumbleProbe: controller 0 found on attempt %d", attempt + 1);

      XINPUT_VIBRATION vibration{};
      vibration.wLeftMotorSpeed = 65535;
      vibration.wRightMotorSpeed = 45000;
      DWORD startResult = XInputSetState(0, &vibration);
      Log("RumbleProbe: XInputSetState START result=%lu left=%u right=%u",
          static_cast<unsigned long>(startResult),
          static_cast<unsigned int>(vibration.wLeftMotorSpeed),
          static_cast<unsigned int>(vibration.wRightMotorSpeed));

      Sleep(800);

      XINPUT_VIBRATION stop{};
      DWORD stopResult = XInputSetState(0, &stop);
      Log("RumbleProbe: XInputSetState STOP result=%lu", static_cast<unsigned long>(stopResult));
      return 0;
    }

    if (attempt == 0 || attempt == 9 || attempt == 24 || attempt == 49)
      Log("RumbleProbe: XInputGetState attempt %d result=%lu", attempt + 1, static_cast<unsigned long>(getResult));

    Sleep(100);
  }

  Log("RumbleProbe: controller 0 not found");
  return 0;
}

static void Start() {
  HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
  if (!thread) {
    Log("RumbleProbe: CreateThread failed error=%lu", static_cast<unsigned long>(GetLastError()));
    return;
  }
  CloseHandle(thread);
}

} // namespace XInputRumbleProbe
#endif
