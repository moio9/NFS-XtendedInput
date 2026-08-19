#pragma once

#ifdef GAME_CARBON

#include <windows.h>
#include <XInput.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdint>

namespace CarbonRealRumble {

static_assert(sizeof(void*) == 4, "Carbon rumble backend requires a 32-bit build");
static_assert(sizeof(InputDevice) == 0x2C, "Unexpected Carbon InputDevice layout; IFeedback must begin at 0x2C");

static inline float Saturate(float v) {
  if (v < 0.0f) return 0.0f;
  if (v > 1.0f) return 1.0f;
  return v;
}

static inline float AbsF(float v) {
  return v < 0.0f ? -v : v;
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

// This vtable/order and the method signatures come from Xan's historical
// rumble-test branch. Carbon's LocalPlayer::DoFFB calls this interface.
class Feedback {
 public:
  void* UTL_List_Owner;

 private:
  int   mDeviceIndex;
  bool  mEnabled;
  bool  mPaused;
  float mFrameLow;
  float mFrameHigh;
  float mLastLow;
  float mLastHigh;
  DWORD mCollisionUntil;
  DWORD mShiftUntil;
  DWORD mEngineBlownUntil;
  float mCollisionLow;
  float mCollisionHigh;
  unsigned int mFrameCounter;
  unsigned int mLoggedMask;

  void LogOnce(unsigned int bit, const char* fmt, ...) {
    if (mLoggedMask & bit) return;
    mLoggedMask |= bit;
    FILE* f = std::fopen("NFS_XtendedInput_Rumble.log", "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(f, fmt, ap);
    va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
  }

  static float SlipStrength(float value) {
    float v = AbsF(value);
    // Slip angles are normally small floats, but this remains safe if a build
    // supplies degrees or another larger unit.
    if (v <= 2.0f) return Saturate(v * 0.85f);
    return Saturate(v / 45.0f);
  }

  static float RPMStrength(float a, float b, float c) {
    a = AbsF(a);
    b = AbsF(b);
    c = AbsF(c);

    // Prefer a plausible current/max pair if Carbon supplied one.
    if (b > 100.0f && a <= b * 1.5f) return Saturate(a / b);
    if (c > 100.0f && a <= c * 1.5f) return Saturate(a / c);

    float v = a;
    if (b > v) v = b;
    if (c > v) v = c;
    if (v <= 1.5f) return Saturate(v);
    return Saturate(v / (v + 4000.0f));
  }

  void AddLow(float amount) {
    amount = Saturate(amount);
    if (amount > mFrameLow) mFrameLow = amount;
  }

  void AddHigh(float amount) {
    amount = Saturate(amount);
    if (amount > mFrameHigh) mFrameHigh = amount;
  }

  void StopHardware() {
    XINPUT_VIBRATION vibration{};
    XInputSetState(static_cast<DWORD>(mDeviceIndex), &vibration);
    mLastLow  = 0.0f;
    mLastHigh = 0.0f;
  }

  void Apply() {
    if (!mEnabled || mPaused || mDeviceIndex < 0 || mDeviceIndex >= XUSER_MAX_COUNT) {
      if (mLastLow != 0.0f || mLastHigh != 0.0f) StopHardware();
      return;
    }

    DWORD now = GetTickCount();
    float low = mFrameLow;
    float high = mFrameHigh;

    if (static_cast<LONG>(mCollisionUntil - now) > 0) {
      if (mCollisionLow > low) low = mCollisionLow;
      if (mCollisionHigh > high) high = mCollisionHigh;
    }
    if (static_cast<LONG>(mShiftUntil - now) > 0) {
      if (0.42f > low) low = 0.42f;
      if (0.48f > high) high = 0.48f;
    }
    if (static_cast<LONG>(mEngineBlownUntil - now) > 0) {
      if (0.95f > low) low = 0.95f;
      if (0.72f > high) high = 0.72f;
    }

    low  = Saturate(low);
    high = Saturate(high);

    // Avoid hammering XInput with identical values every frame.
    if (AbsF(low - mLastLow) < 0.008f && AbsF(high - mLastHigh) < 0.008f) return;

    XINPUT_VIBRATION vibration{};
    vibration.wLeftMotorSpeed  = static_cast<WORD>(low * 65535.0f);
    vibration.wRightMotorSpeed = static_cast<WORD>(high * 65535.0f);
    DWORD result = XInputSetState(static_cast<DWORD>(mDeviceIndex), &vibration);

    mLastLow  = low;
    mLastHigh = high;

    if ((low > 0.03f || high > 0.03f) && ((mFrameCounter % 60u) == 0u)) {
      Log("RealRumble: output dev=%d low=%.3f high=%.3f result=%lu",
          mDeviceIndex, low, high, static_cast<unsigned long>(result));
    }
  }

 public:
  Feedback(int deviceIndex = 0)
      : UTL_List_Owner(nullptr),
        mDeviceIndex(deviceIndex),
        mEnabled(true),
        mPaused(false),
        mFrameLow(0.0f),
        mFrameHigh(0.0f),
        mLastLow(0.0f),
        mLastHigh(0.0f),
        mCollisionUntil(0),
        mShiftUntil(0),
        mEngineBlownUntil(0),
        mCollisionLow(0.0f),
        mCollisionHigh(0.0f),
        mFrameCounter(0),
        mLoggedMask(0) {}

  void SetOwner(void* owner) { UTL_List_Owner = owner; }
  void SetDeviceIndex(int index) { mDeviceIndex = index; }

  void Enable() {
    mEnabled = true;
    mPaused  = false;
    LogOnce(1u << 0, "RealRumble: vibration enabled dev=%d", mDeviceIndex);
  }

  void Disable() {
    mEnabled = false;
    StopHardware();
  }

  void StopNow() { StopHardware(); }

  virtual void* dtor(bool) {
    StopHardware();
    // Embedded in RumbleInputDevice: never delete this subobject independently.
    return this;
  }

  virtual void PauseEffects() {
    mPaused = true;
    StopHardware();
  }

  virtual void ResumeEffects() {
    mPaused = false;
    Apply();
  }

  virtual void ResetEffects() {
    mFrameLow = mFrameHigh = 0.0f;
    mCollisionUntil = mShiftUntil = mEngineBlownUntil = 0;
    mCollisionLow = mCollisionHigh = 0.0f;
    StopHardware();
  }

  virtual void BeginUpdate() {
    ++mFrameCounter;
    mFrameLow  = 0.0f;
    mFrameHigh = 0.0f;
  }

  virtual void EndUpdate() {
    Apply();
  }

  virtual void UpdateRoadNoise(int wheel, void* simSurface, float slipAngle) {
    float s = SlipStrength(slipAngle);
    // Constant low-frequency texture plus surface/slip-dependent detail.
    if (simSurface) AddLow(0.055f + s * 0.16f);
    AddHigh(s * 0.10f);
    LogOnce(1u << 1, "RealRumble: UpdateRoadNoise wheel=%d surface=%p slip=%.4f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateTireSkid(int wheel, void* simSurface, float slipAngle) {
    float s = SlipStrength(slipAngle);
    AddLow(s * 0.34f);
    AddHigh(s * 0.72f);
    LogOnce(1u << 2, "RealRumble: UpdateTireSkid wheel=%d surface=%p slip=%.4f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateTireSlip(int wheel, void* simSurface, float slipAngle) {
    float s = SlipStrength(slipAngle);
    AddLow(s * 0.24f);
    AddHigh(s * 0.46f);
    LogOnce(1u << 3, "RealRumble: UpdateTireSlip wheel=%d surface=%p slip=%.4f", wheel, simSurface, slipAngle);
  }

  virtual void UpdateRPM(float a, float b, float c) {
    float rpm = RPMStrength(a, b, c);
    // Engine texture: mostly the large/low-frequency motor, deliberately mild.
    AddLow(0.035f + rpm * 0.20f);
    AddHigh(rpm * 0.055f);
    LogOnce(1u << 4, "RealRumble: UpdateRPM a=%.3f b=%.3f c=%.3f normalized=%.3f", a, b, c, rpm);
  }

  virtual void UpdateShiftPotential(int shiftPotential) {
    if (shiftPotential >= 3) AddHigh(0.10f);
    LogOnce(1u << 5, "RealRumble: UpdateShiftPotential value=%d", shiftPotential);
  }

  virtual void UpdateNOS(int engaged, float nosAmount) {
    if (engaged) {
      float amount = AbsF(nosAmount);
      if (amount > 1.0f) amount = amount / (amount + 1.0f);
      amount = Saturate(amount);
      AddLow(0.46f + amount * 0.20f);
      AddHigh(0.68f + amount * 0.22f);
    }
    LogOnce(1u << 6, "RealRumble: UpdateNOS engaged=%d amount=%.3f", engaged, nosAmount);
  }

  virtual void UpdateEngineBlown(int state) {
    if (state) mEngineBlownUntil = GetTickCount() + 650;
    LogOnce(1u << 7, "RealRumble: UpdateEngineBlown state=%d", state);
  }

  virtual void UpdateShifting(int state) {
    if (state) mShiftUntil = GetTickCount() + 115;
    LogOnce(1u << 8, "RealRumble: UpdateShifting state=%d", state);
  }

  virtual void UpdateDrafting() {
    AddHigh(0.075f);
    LogOnce(1u << 9, "RealRumble: UpdateDrafting called");
  }

  virtual void UpdateDrifSlip() {
    AddLow(0.18f);
    AddHigh(0.34f);
    LogOnce(1u << 10, "RealRumble: UpdateDrifSlip called");
  }

  virtual void UpdateDrifSkid() {
    AddLow(0.25f);
    AddHigh(0.52f);
    LogOnce(1u << 11, "RealRumble: UpdateDrifSkid called");
  }

  virtual void UpdateSteering() {
    // Intentionally no constant motor force for steering on an XInput pad.
    LogOnce(1u << 12, "RealRumble: UpdateSteering called");
  }

  virtual void UpdateShockDigression() {
    // Small suspension/road kick. If Carbon calls this continuously, max-combine
    // keeps it as a subtle texture rather than an accumulating effect.
    AddLow(0.13f);
    AddHigh(0.06f);
    LogOnce(1u << 13, "RealRumble: UpdateShockDigression called");
  }

  virtual void ReportCollision(void* collisionInfo, int kind) {
    mCollisionUntil = GetTickCount() + 190;
    // Collision information is opaque here; Carbon itself decides when this
    // callback is warranted, so use a strong short impact pulse.
    mCollisionLow  = 0.92f;
    mCollisionHigh = 0.58f;
    LogOnce(1u << 14, "RealRumble: ReportCollision info=%p kind=%d", collisionInfo, kind);
    Apply();
  }
};

// Deriving rather than modifying InputDevice keeps upstream input behavior intact.
// On x86 InputDevice is exactly 0x2C bytes, so the embedded Feedback object begins
// at the offset expected by Black Box's GameDevice layout.
class RumbleInputDevice : public InputDevice {
 public:
  Feedback mFFB;

  explicit RumbleInputDevice(int deviceIndex)
      : InputDevice(deviceIndex), mFFB(deviceIndex) {
    mFFB.SetOwner(reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(this) + 4u));
    Log("RealRumble: device created index=%d base=%p feedback=%p offset=0x%X",
        deviceIndex, this, &mFFB,
        static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&mFFB) - reinterpret_cast<std::uintptr_t>(this)));
  }

  virtual void* dtor(bool) override {
    mFFB.StopNow();
    delete this;
    return this;
  }

  virtual void StartVibration() override {
    mFFB.Enable();
  }

  virtual void StopVibration() override {
    mFFB.Disable();
  }

  virtual int GetInterfaces() override {
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(&mFFB));
  }
};

static InputDevice* RumbleInputDeviceFactory(int deviceIndex) {
  return new RumbleInputDevice(deviceIndex);
}

static void Install() {
  std::remove("NFS_XtendedInput_Rumble.log");
  Log("RealRumble: installing Carbon IFeedback/XInput backend");
  Log("RealRumble: sizeof(InputDevice)=0x%X sizeof(RumbleInputDevice)=0x%X",
      static_cast<unsigned int>(sizeof(InputDevice)), static_cast<unsigned int>(sizeof(RumbleInputDevice)));

  // Init() writes the normal XtendedInput factory first. Override it immediately
  // afterwards, before Carbon creates its InputDevice objects.
  injector::WriteMemory<unsigned int>(INPUTDEVICE_FACTORY_INITIALIZER_ADDR,
                                      static_cast<unsigned int>(reinterpret_cast<std::uintptr_t>(&RumbleInputDeviceFactory)),
                                      true);
  Log("RealRumble: factory installed at 0x%08X", static_cast<unsigned int>(INPUTDEVICE_FACTORY_INITIALIZER_ADDR));
}

}  // namespace CarbonRealRumble

#endif  // GAME_CARBON
