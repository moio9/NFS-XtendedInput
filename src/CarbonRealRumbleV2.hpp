#pragma once

#ifdef GAME_CARBON

#include <windows.h>
#include <XInput.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>

namespace CarbonRealRumbleV2 {

static_assert(sizeof(void*) == 4, "Carbon rumble backend requires x86");
static_assert(sizeof(InputDevice) == 0x2C, "Unexpected Carbon InputDevice layout");

constexpr std::uintptr_t kUTLIListAddAddr = 0x0060DCB0;
constexpr std::uintptr_t kFeedbackIHandle = 0x00679170;
constexpr std::uintptr_t kGetInterfacesDispAddr = 0x007651F4;   // original 0x24
constexpr std::uintptr_t kGetSecondaryDispAddr  = 0x00765213;   // original 0x28

static inline float Saturate(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float AbsF(float v) { return v < 0.0f ? -v : v; }

static inline float Quantize(float v) {
  v = Saturate(v);
  return std::floor(v * 16.0f + 0.5f) / 16.0f;
}

static void Log(const char* fmt, ...) {
  FILE* f = std::fopen("NFS_XtendedInput_Rumble.log", "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

class Feedback;
class RumbleInputDevice;

struct FeedbackState {
  Feedback* iface;
  int deviceIndex;
  bool enabled;
  bool paused;
  float frameLow;
  float frameHigh;
  float lastLow;
  float lastHigh;
  float collisionLow;
  float collisionHigh;
  DWORD collisionUntil;
  DWORD shiftUntil;
  DWORD engineBlownUntil;
  DWORD lastSendTick;
  DWORD lastActiveTick;
  DWORD backoffUntil;
  unsigned int frameCounter;
  unsigned int loggedMask;
  unsigned int callbackCount[18];
};

static FeedbackState g_states[8]{};

typedef void*(__thiscall* UTL_IList_Add_t)(void*, void*, void*);
static UTL_IList_Add_t UTL_IList_Add = reinterpret_cast<UTL_IList_Add_t>(kUTLIListAddAddr);

static FeedbackState* FindState(Feedback* iface) {
  for (auto& s : g_states) {
    if (s.iface == iface) return &s;
  }
  return nullptr;
}

static FeedbackState* RegisterState(Feedback* iface, int deviceIndex) {
  if (auto* old = FindState(iface)) return old;
  for (auto& s : g_states) {
    if (!s.iface) {
      s = FeedbackState{};
      s.iface = iface;
      s.deviceIndex = deviceIndex;
      s.enabled = true;
      s.lastActiveTick = GetTickCount();
      return &s;
    }
  }
  return nullptr;
}

static void UnregisterState(Feedback* iface) {
  if (auto* s = FindState(iface)) *s = FeedbackState{};
}

static void LogCallback(FeedbackState* s, unsigned int idx, const char* fmt, ...) {
  if (!s || idx >= 18) return;
  unsigned int n = ++s->callbackCount[idx];
  if (n > 4 && (n % 240u) != 0u) return;

  FILE* f = std::fopen("NFS_XtendedInput_Rumble.log", "a");
  if (!f) return;
  std::fprintf(f, "RealRumbleV2: cb[%u] #%u ", idx, n);
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(f, fmt, ap);
  va_end(ap);
  std::fputc('\n', f);
  std::fclose(f);
}

static void SendHardware(FeedbackState* s, float low, float high, bool force = false) {
  if (!s || s->deviceIndex < 0 || s->deviceIndex >= XUSER_MAX_COUNT) return;

  DWORD now = GetTickCount();
  if (!force) {
    if (static_cast<LONG>(s->backoffUntil - now) > 0) return;
    if (now - s->lastSendTick < 50) return;  // 20 Hz maximum to the DInput rumble proxy
  }

  low = Quantize(low);
  high = Quantize(high);

  if (!force && AbsF(low - s->lastLow) < 0.061f && AbsF(high - s->lastHigh) < 0.061f) return;

  XINPUT_VIBRATION vibration{};
  vibration.wLeftMotorSpeed = static_cast<WORD>(low * 65535.0f);
  vibration.wRightMotorSpeed = static_cast<WORD>(high * 65535.0f);

  DWORD result = XInputSetState(static_cast<DWORD>(s->deviceIndex), &vibration);
  s->lastSendTick = now;

  if (result == ERROR_SUCCESS) {
    s->lastLow = low;
    s->lastHigh = high;
  } else {
    s->backoffUntil = now + 500;
    Log("RealRumbleV2: XInputSetState failed dev=%d result=%lu; backing off", s->deviceIndex,
        static_cast<unsigned long>(result));
  }

  if ((low > 0.0f || high > 0.0f) && ((s->frameCounter % 120u) == 0u)) {
    Log("RealRumbleV2: output dev=%d low=%.3f high=%.3f result=%lu", s->deviceIndex, low, high,
        static_cast<unsigned long>(result));
  }
}

static void StopHardware(FeedbackState* s, bool force = false) {
  if (!s) return;
  if (!force && s->lastLow == 0.0f && s->lastHigh == 0.0f) return;
  SendHardware(s, 0.0f, 0.0f, true);
}

static void AddLow(FeedbackState* s, float amount) {
  if (!s) return;
  amount = Saturate(amount);
  if (amount > s->frameLow) s->frameLow = amount;
}

static void AddHigh(FeedbackState* s, float amount) {
  if (!s) return;
  amount = Saturate(amount);
  if (amount > s->frameHigh) s->frameHigh = amount;
}

static float SlipStrength(float value) {
  float v = AbsF(value);
  if (v <= 2.0f) return Saturate(v * 0.85f);
  return Saturate(v / 45.0f);
}

static float RPMStrength(float a, float b, float c) {
  a = AbsF(a);
  b = AbsF(b);
  c = AbsF(c);
  if (b > 100.0f && a <= b * 1.5f) return Saturate(a / b);
  if (c > 100.0f && a <= c * 1.5f) return Saturate(a / c);
  float v = a;
  if (b > v) v = b;
  if (c > v) v = c;
  if (v <= 1.5f) return Saturate(v);
  return Saturate(v / (v + 4000.0f));
}

static void Apply(FeedbackState* s) {
  if (!s) return;

  DWORD now = GetTickCount();
  float low = 0.0f;
  float high = 0.0f;

  if (s->enabled && !s->paused) {
    low = s->frameLow;
    high = s->frameHigh;

    if (static_cast<LONG>(s->collisionUntil - now) > 0) {
      if (s->collisionLow > low) low = s->collisionLow;
      if (s->collisionHigh > high) high = s->collisionHigh;
    }
    if (static_cast<LONG>(s->shiftUntil - now) > 0) {
      if (0.40f > low) low = 0.40f;
      if (0.46f > high) high = 0.46f;
    }
    if (static_cast<LONG>(s->engineBlownUntil - now) > 0) {
      if (0.90f > low) low = 0.90f;
      if (0.70f > high) high = 0.70f;
    }
  }

  low = Saturate(low);
  high = Saturate(high);

  if (low > 0.025f || high > 0.025f) {
    s->lastActiveTick = now;
    SendHardware(s, low, high, false);
    return;
  }

  // Do not start/stop the DirectInput effect on alternating frames. Keep the last
  // output briefly and send one zero only after the game has been quiet for 120ms.
  if ((s->lastLow != 0.0f || s->lastHigh != 0.0f) && now - s->lastActiveTick >= 120) {
    SendHardware(s, 0.0f, 0.0f, false);
  }
}

// Exact Carbon PC IFeedback ABI from the game's vtable at 0x009E2188.
// The interface object MUST remain 8 bytes: vptr + UTL_List_Owner.
class Feedback {
 public:
  void* UTL_List_Owner;

  virtual void* dtor(bool something);

  virtual void PauseEffects() {
    auto* s = FindState(this);
    if (s) s->paused = true;
    LogCallback(s, 1, "PauseEffects");
  }

  virtual void ResumeEffects() {
    auto* s = FindState(this);
    if (s) s->paused = false;
    LogCallback(s, 2, "ResumeEffects");
  }

  virtual void ResetEffects() {
    auto* s = FindState(this);
    if (s) {
      s->frameLow = s->frameHigh = 0.0f;
      s->collisionUntil = s->shiftUntil = s->engineBlownUntil = 0;
      s->collisionLow = s->collisionHigh = 0.0f;
      StopHardware(s, false);
    }
    LogCallback(s, 3, "ResetEffects");
  }

  virtual void BeginUpdate() {
    auto* s = FindState(this);
    if (s) {
      ++s->frameCounter;
      s->frameLow = 0.0f;
      s->frameHigh = 0.0f;
    }
    LogCallback(s, 4, "BeginUpdate");
  }

  virtual void EndUpdate() {
    auto* s = FindState(this);
    Apply(s);
    LogCallback(s, 5, "EndUpdate");
  }

  virtual void UpdateRoadNoise(int wheel, void* simSurface, float slipAngle) {
    auto* s = FindState(this);
    float strength = SlipStrength(slipAngle);
    if (simSurface) AddLow(s, 0.035f + strength * 0.13f);
    AddHigh(s, strength * 0.08f);
    LogCallback(s, 6, "Road wheel=%d surface=%p slip=%.5f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateTireSkid(int wheel, void* simSurface, float slipAngle) {
    auto* s = FindState(this);
    float strength = SlipStrength(slipAngle);
    AddLow(s, strength * 0.28f);
    AddHigh(s, strength * 0.62f);
    LogCallback(s, 7, "Skid wheel=%d surface=%p slip=%.5f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateTireSlip(int wheel, void* simSurface, float slipAngle) {
    auto* s = FindState(this);
    float strength = SlipStrength(slipAngle);
    AddLow(s, strength * 0.20f);
    AddHigh(s, strength * 0.38f);
    LogCallback(s, 8, "Slip wheel=%d surface=%p slip=%.5f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateRPM(float a, float b, float c) {
    auto* s = FindState(this);
    float rpm = RPMStrength(a, b, c);
    // Quantization + 20Hz hardware cap prevents RT/RPM from hammering the DInput proxy.
    AddLow(s, rpm * 0.20f);
    AddHigh(s, rpm * 0.045f);
    LogCallback(s, 9, "RPM a=%.3f b=%.3f c=%.3f norm=%.3f", a, b, c, rpm);
  }

  virtual void UpdateShiftPotential(int value) {
    auto* s = FindState(this);
    if (value >= 3) AddHigh(s, 0.09f);
    LogCallback(s, 10, "ShiftPotential=%d", value);
  }

  virtual void UpdateNOS(int engaged, float amountRaw) {
    auto* s = FindState(this);
    if (engaged) {
      float amount = AbsF(amountRaw);
      if (amount > 1.0f) amount = amount / (amount + 1.0f);
      amount = Saturate(amount);
      AddLow(s, 0.44f + amount * 0.18f);
      AddHigh(s, 0.62f + amount * 0.20f);
    }
    LogCallback(s, 11, "NOS engaged=%d amount=%.4f", engaged, amountRaw);
  }

  virtual void UpdateEngineBlown(int state) {
    auto* s = FindState(this);
    if (s && state) s->engineBlownUntil = GetTickCount() + 600;
    LogCallback(s, 12, "EngineBlown=%d", state);
  }

  virtual void UpdateShifting(int state) {
    auto* s = FindState(this);
    if (s && state) s->shiftUntil = GetTickCount() + 120;
    LogCallback(s, 13, "Shifting=%d", state);
  }

  // Carbon PC has exactly three one-argument slots here. Their semantic names
  // were removed from the PC build, so keep their ABI exact and log raw values
  // before assigning effects to them.
  virtual void UpdateExtra0(std::uint32_t raw) {
    auto* s = FindState(this);
    LogCallback(s, 14, "Extra0 raw=0x%08X", raw);
  }

  virtual void UpdateExtra1(std::uint32_t raw) {
    auto* s = FindState(this);
    LogCallback(s, 15, "Extra1 raw=0x%08X", raw);
  }

  virtual void UpdateExtra2(std::uint32_t raw) {
    auto* s = FindState(this);
    LogCallback(s, 16, "Extra2 raw=0x%08X", raw);
  }

  virtual void ReportCollision(void* collisionInfo, int kind) {
    auto* s = FindState(this);
    if (s) {
      s->collisionUntil = GetTickCount() + 190;
      s->collisionLow = 0.88f;
      s->collisionHigh = 0.54f;
    }
    LogCallback(s, 17, "Collision info=%p kind=%d", collisionInfo, kind);
  }
};

static_assert(sizeof(Feedback) == 0x08, "Carbon IFeedback must be exactly 8 bytes");

class RumbleInputDevice : public InputDevice {
 public:
  Feedback mFFB;

  explicit RumbleInputDevice(int deviceIndex) : InputDevice(deviceIndex), mFFB{} {
    auto* list = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + 4u);
    mFFB.UTL_List_Owner = list;
    RegisterState(&mFFB, deviceIndex);

    // Reproduce the original Carbon GameDevice constructor:
    // IList::Add(FeedbackIHandle, &mFFB), with IFeedback at +0x2C.
    UTL_IList_Add(list, reinterpret_cast<void*>(kFeedbackIHandle), &mFFB);

    Log("RealRumbleV2: device index=%d base=%p feedback=%p offset=0x%X sizeofFeedback=0x%X",
        deviceIndex, this, &mFFB,
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&mFFB) - reinterpret_cast<std::uintptr_t>(this)),
        static_cast<unsigned int>(sizeof(Feedback)));
  }

  virtual void* dtor(bool) override {
    if (auto* s = FindState(&mFFB)) StopHardware(s, true);
    UnregisterState(&mFFB);
    delete this;
    return this;
  }

  virtual void StartVibration() override {
    if (auto* s = FindState(&mFFB)) s->enabled = true;
  }

  virtual void StopVibration() override {
    if (auto* s = FindState(&mFFB)) {
      s->enabled = false;
      StopHardware(s, false);
    }
  }

  virtual int GetInterfaces() override {
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(&mFFB));
  }
};

static_assert(sizeof(RumbleInputDevice) == 0x34, "RumbleInputDevice must end immediately after the 8-byte IFeedback");

inline void* Feedback::dtor(bool something) {
  auto* owner = reinterpret_cast<RumbleInputDevice*>(reinterpret_cast<std::uintptr_t>(this) - 0x2Cu);
  return owner->dtor(something);
}

static InputDevice* RumbleInputDeviceFactory(int deviceIndex) {
  return new RumbleInputDevice(deviceIndex);
}

static void Install() {
  std::remove("NFS_XtendedInput_Rumble.log");
  Log("RealRumbleV2: installing exact Carbon IFeedback ABI");
  Log("RealRumbleV2: InputDevice=0x%X Feedback=0x%X RumbleInputDevice=0x%X",
      static_cast<unsigned int>(sizeof(InputDevice)), static_cast<unsigned int>(sizeof(Feedback)),
      static_cast<unsigned int>(sizeof(RumbleInputDevice)));
  Log("RealRumbleV2: IListAdd=0x%08X FeedbackIHandle=0x%08X",
      static_cast<unsigned int>(kUTLIListAddAddr), static_cast<unsigned int>(kFeedbackIHandle));

  // XtendedInput's current InputDevice vtable omits Carbon's empty slot at index 8.
  // Patch only LocalPlayer's two interface lookups so they call XtendedInput's actual
  // GetInterfaces (+0x20) and GetSecondaryDevice (+0x24).
  injector::WriteMemory<unsigned char>(kGetInterfacesDispAddr, 0x20, true);
  injector::WriteMemory<unsigned char>(kGetSecondaryDispAddr, 0x24, true);

  injector::WriteMemory<unsigned int>(INPUTDEVICE_FACTORY_INITIALIZER_ADDR,
                                      static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&RumbleInputDeviceFactory)),
                                      true);

  Log("RealRumbleV2: patched LocalPlayer interface slots and installed factory");
}

}  // namespace CarbonRealRumbleV2

#endif  // GAME_CARBON
