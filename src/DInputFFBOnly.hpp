#pragma once
#ifdef GAME_CARBON
#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif
#include <windows.h>
#include <dinput.h>
#include <shlwapi.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace DInputFFBOnly {

constexpr DWORD kDInputKillRVA = 0x006892EBu - 0x00400000u;
constexpr size_t kPatchSize = 5;
constexpr size_t kMaxObjects = 128;

using DirectInput8CreateFn = HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,LPVOID*,LPUNKNOWN);
using CreateDeviceFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInput8A*,REFGUID,LPDIRECTINPUTDEVICE8A*,LPUNKNOWN);
using GetDeviceStateFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,DWORD,LPVOID);
using GetDeviceDataFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,DWORD,LPDIDEVICEOBJECTDATA,LPDWORD,DWORD);
using SetDataFormatFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,LPCDIDATAFORMAT);
using AcquireFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*);
using SetCooperativeLevelFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,HWND,DWORD);
using CreateEffectFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,REFGUID,LPCDIEFFECT,LPDIRECTINPUTEFFECT*,LPUNKNOWN);
using SendForceFeedbackCommandFn = HRESULT(STDMETHODCALLTYPE*)(IDirectInputDevice8A*,DWORD);

struct NeutralObject { DWORD ofs, type; LONG neutral; };
static DirectInput8CreateFn g_realDI8Create = nullptr;
static CreateDeviceFn g_realCreateDevice = nullptr;
static GetDeviceStateFn g_realGetDeviceState = nullptr;
static GetDeviceDataFn g_realGetDeviceData = nullptr;
static SetDataFormatFn g_realSetDataFormat = nullptr;
static AcquireFn g_realAcquire = nullptr;
static SetCooperativeLevelFn g_realSetCooperativeLevel = nullptr;
static CreateEffectFn g_realCreateEffect = nullptr;
static SendForceFeedbackCommandFn g_realSendForceFeedbackCommand = nullptr;
static IDirectInputDevice8A* g_blocked = nullptr;
static NeutralObject g_objects[kMaxObjects]{};
static DWORD g_objectCount = 0;
static bool g_neutralReady = false;
static BYTE g_originalBytes[kPatchSize]{};
static bool g_haveOriginalBytes = false;

static void Log(const char* fmt, ...) {
  FILE* f = std::fopen("NFS_XtendedInput_DInputFFBOnly.log", "a");
  if (!f) return;
  va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
  std::fputc('\n', f); std::fclose(f);
}

static bool PatchPtr(void** p, void* value, void** old = nullptr) {
  if (!p || !value) return false;
  DWORD prot = 0; if (!VirtualProtect(p, sizeof(void*), PAGE_EXECUTE_READWRITE, &prot)) return false;
  if (old) *old = *p; *p = value; FlushInstructionCache(GetCurrentProcess(), p, sizeof(void*));
  DWORD tmp = 0; VirtualProtect(p, sizeof(void*), prot, &tmp); return true;
}

static bool PatchBytes(void* p, const void* src, size_t n) {
  DWORD prot = 0; if (!VirtualProtect(p, n, PAGE_EXECUTE_READWRITE, &prot)) return false;
  std::memcpy(p, src, n); FlushInstructionCache(GetCurrentProcess(), p, n);
  DWORD tmp = 0; VirtualProtect(p, n, prot, &tmp); return true;
}

static bool IsNops(const BYTE* p) {
  for (size_t i=0;i<kPatchSize;i++) if (p[i] != 0x90) return false;
  return true;
}

static bool HookImport(HMODULE module, const char* dll, const char* name, void* hook, void** original) {
  auto* base = reinterpret_cast<BYTE*>(module);
  auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (!dos || dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
  auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
  auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if (!dir.VirtualAddress) return false;
  auto* desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
  for (; desc->Name; ++desc) {
    const char* imported = reinterpret_cast<const char*>(base + desc->Name);
    if (_stricmp(imported, dll)) continue;
    auto* ft = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + desc->FirstThunk);
    if (desc->OriginalFirstThunk) {
      auto* oft = reinterpret_cast<IMAGE_THUNK_DATA32*>(base + desc->OriginalFirstThunk);
      for (; oft->u1.AddressOfData; ++oft, ++ft) {
        if (IMAGE_SNAP_BY_ORDINAL32(oft->u1.Ordinal)) continue;
        auto* ibn = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + oft->u1.AddressOfData);
        if (std::strcmp(reinterpret_cast<const char*>(ibn->Name), name)) continue;
        return PatchPtr(reinterpret_cast<void**>(&ft->u1.Function), hook, original);
      }
    } else {
      HMODULE m = GetModuleHandleA(imported);
      FARPROC target = m ? GetProcAddress(m, name) : nullptr;
      Log("DInputFFBOnly: no OriginalFirstThunk; fallback target=%p", reinterpret_cast<void*>(target));
      if (!target) return false;
      for (; ft->u1.Function; ++ft) {
        if (reinterpret_cast<void*>(static_cast<uintptr_t>(ft->u1.Function)) != reinterpret_cast<void*>(target)) continue;
        return PatchPtr(reinterpret_cast<void**>(&ft->u1.Function), hook, original);
      }
    }
  }
  return false;
}

static bool IsGamepad(IDirectInputDevice8A* dev, bool& hasFFB, char* name, size_t nameSize) {
  DIDEVCAPS caps{}; caps.dwSize = sizeof(caps);
  if (FAILED(dev->GetCapabilities(&caps))) return false;
  hasFFB = (caps.dwFlags & DIDC_FORCEFEEDBACK) != 0;
  DIDEVICEINSTANCEA info{}; info.dwSize = sizeof(info);
  if (SUCCEEDED(dev->GetDeviceInfo(&info))) {
    std::strncpy(name, info.tszProductName, nameSize - 1); name[nameSize - 1] = 0;
  }
  DWORD type = GET_DIDEVICE_TYPE(caps.dwDevType);
  if (type == DI8DEVTYPE_GAMEPAD) return true;
  if (type != DI8DEVTYPE_JOYSTICK) return false;
  return StrStrIA(name, "xbox") || StrStrIA(name, "xinput") || StrStrIA(name, "controller");
}

static void BuildNeutral(IDirectInputDevice8A* dev) {
  for (DWORD i=0;i<g_objectCount;i++) {
    if (!(g_objects[i].type & DIDFT_AXIS)) continue;
    if (g_objects[i].type & DIDFT_RELAXIS) { g_objects[i].neutral = 0; continue; }
    DIPROPRANGE r{}; r.diph.dwSize=sizeof(r); r.diph.dwHeaderSize=sizeof(r.diph);
    r.diph.dwObj=g_objects[i].ofs; r.diph.dwHow=DIPH_BYOFFSET;
    if (SUCCEEDED(dev->GetProperty(DIPROP_RANGE, &r.diph)))
      g_objects[i].neutral = r.lMin + (r.lMax-r.lMin)/2;
    else g_objects[i].neutral = 32767;
  }
  g_neutralReady = true;
}

static HRESULT STDMETHODCALLTYPE HookSetDataFormat(IDirectInputDevice8A* self, LPCDIDATAFORMAT fmt) {
  HRESULT hr = g_realSetDataFormat(self, fmt);
  if (FAILED(hr) || self != g_blocked) return hr;
  g_objectCount = 0; g_neutralReady = false;
  if (fmt && fmt->rgodf) {
    for (DWORD i=0;i<fmt->dwNumObjs && g_objectCount<kMaxObjects;i++) {
      if (fmt->rgodf[i].dwType & DIDFT_NODATA) continue;
      g_objects[g_objectCount++] = {fmt->rgodf[i].dwOfs, fmt->rgodf[i].dwType, 0};
    }
  }
  Log("DInputFFBOnly: captured format: %lu objects, %lu bytes",
      static_cast<unsigned long>(g_objectCount), fmt ? static_cast<unsigned long>(fmt->dwDataSize) : 0ul);
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookGetDeviceState(IDirectInputDevice8A* self, DWORD size, LPVOID data) {
  HRESULT hr = g_realGetDeviceState(self, size, data);
  if (FAILED(hr) || self != g_blocked || !data) return hr;
  if (!g_neutralReady) BuildNeutral(self);
  auto* b = reinterpret_cast<BYTE*>(data);
  for (DWORD i=0;i<g_objectCount;i++) {
    DWORD o=g_objects[i].ofs, t=g_objects[i].type;
    if (o >= size) continue;
    if ((t & DIDFT_AXIS) && o+sizeof(LONG)<=size) *reinterpret_cast<LONG*>(b+o)=g_objects[i].neutral;
    else if ((t & DIDFT_POV) && o+sizeof(DWORD)<=size) *reinterpret_cast<DWORD*>(b+o)=0xFFFFFFFFu;
    else if (t & DIDFT_BUTTON) b[o]=0;
  }
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookGetDeviceData(IDirectInputDevice8A* self, DWORD objSize,
    LPDIDEVICEOBJECTDATA data, LPDWORD count, DWORD flags) {
  HRESULT hr = g_realGetDeviceData(self, objSize, data, count, flags);
  if (self == g_blocked && (SUCCEEDED(hr) || hr == DI_BUFFEROVERFLOW) && count) { *count=0; return DI_OK; }
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookAcquire(IDirectInputDevice8A* self) {
  HRESULT hr = g_realAcquire(self);
  if (self == g_blocked) Log("DInputFFBOnly: Acquire hr=0x%08lX", static_cast<unsigned long>(hr));
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookSetCooperativeLevel(IDirectInputDevice8A* self, HWND hwnd, DWORD flags) {
  HRESULT hr = g_realSetCooperativeLevel(self, hwnd, flags);
  if (self == g_blocked)
    Log("DInputFFBOnly: SetCooperativeLevel flags=0x%08lX exclusive=%s background=%s hr=0x%08lX",
        static_cast<unsigned long>(flags), (flags & DISCL_EXCLUSIVE) ? "yes" : "no",
        (flags & DISCL_BACKGROUND) ? "yes" : "no", static_cast<unsigned long>(hr));
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookCreateEffect(IDirectInputDevice8A* self, REFGUID guid, LPCDIEFFECT effect,
    LPDIRECTINPUTEFFECT* out, LPUNKNOWN outer) {
  HRESULT hr = g_realCreateEffect(self, guid, effect, out, outer);
  if (self == g_blocked)
    Log("DInputFFBOnly: CreateEffect guid=%08lX magnitude=%ld duration=%lu hr=0x%08lX effect=%p",
        static_cast<unsigned long>(guid.Data1),
        (effect && effect->lpvTypeSpecificParams && effect->cbTypeSpecificParams >= sizeof(DICONSTANTFORCE))
          ? static_cast<long>(reinterpret_cast<const DICONSTANTFORCE*>(effect->lpvTypeSpecificParams)->lMagnitude) : 0L,
        effect ? static_cast<unsigned long>(effect->dwDuration) : 0UL, static_cast<unsigned long>(hr),
        (SUCCEEDED(hr) && out) ? *out : nullptr);
  return hr;
}

static HRESULT STDMETHODCALLTYPE HookSendForceFeedbackCommand(IDirectInputDevice8A* self, DWORD flags) {
  HRESULT hr = g_realSendForceFeedbackCommand(self, flags);
  if (self == g_blocked) Log("DInputFFBOnly: SendForceFeedbackCommand flags=0x%08lX hr=0x%08lX",
                             static_cast<unsigned long>(flags), static_cast<unsigned long>(hr));
  return hr;
}

static bool HookDevice(IDirectInputDevice8A* dev) {
  void** vt = *reinterpret_cast<void***>(dev); if (!vt) return false;
  if (!g_realAcquire) g_realAcquire = reinterpret_cast<AcquireFn>(vt[7]);
  if (!g_realGetDeviceState) g_realGetDeviceState = reinterpret_cast<GetDeviceStateFn>(vt[9]);
  if (!g_realGetDeviceData)  g_realGetDeviceData  = reinterpret_cast<GetDeviceDataFn>(vt[10]);
  if (!g_realSetDataFormat)  g_realSetDataFormat  = reinterpret_cast<SetDataFormatFn>(vt[11]);
  if (!g_realSetCooperativeLevel) g_realSetCooperativeLevel = reinterpret_cast<SetCooperativeLevelFn>(vt[13]);
  if (!g_realCreateEffect) g_realCreateEffect = reinterpret_cast<CreateEffectFn>(vt[18]);
  if (!g_realSendForceFeedbackCommand) g_realSendForceFeedbackCommand = reinterpret_cast<SendForceFeedbackCommandFn>(vt[22]);
  return PatchPtr(&vt[7], reinterpret_cast<void*>(&HookAcquire)) &&
         PatchPtr(&vt[9], reinterpret_cast<void*>(&HookGetDeviceState)) &&
         PatchPtr(&vt[10], reinterpret_cast<void*>(&HookGetDeviceData)) &&
         PatchPtr(&vt[11], reinterpret_cast<void*>(&HookSetDataFormat)) &&
         PatchPtr(&vt[13], reinterpret_cast<void*>(&HookSetCooperativeLevel)) &&
         PatchPtr(&vt[18], reinterpret_cast<void*>(&HookCreateEffect)) &&
         PatchPtr(&vt[22], reinterpret_cast<void*>(&HookSendForceFeedbackCommand));
}

static HRESULT STDMETHODCALLTYPE HookCreateDevice(IDirectInput8A* self, REFGUID guid,
    LPDIRECTINPUTDEVICE8A* out, LPUNKNOWN outer) {
  HRESULT hr = g_realCreateDevice(self, guid, out, outer);
  if (FAILED(hr) || !out || !*out) return hr;
  char name[MAX_PATH]{}; bool ffb=false;
  bool block = IsGamepad(*out, ffb, name, sizeof(name));
  Log("DInputFFBOnly: CreateDevice '%s' ffb=%s block=%s", name[0]?name:"<unknown>", ffb?"yes":"no", block?"yes":"no");
  if (block && !g_blocked) {
    g_blocked = *out;
    if (HookDevice(g_blocked)) Log("DInputFFBOnly: controller input suppressed; FFB methods untouched");
    else { Log("DInputFFBOnly: device hook failed"); g_blocked=nullptr; }
  }
  return hr;
}

static bool HookDIObject(void* obj) {
  void** vt = *reinterpret_cast<void***>(obj); if (!vt) return false;
  if (!g_realCreateDevice) g_realCreateDevice = reinterpret_cast<CreateDeviceFn>(vt[3]);
  return PatchPtr(&vt[3], reinterpret_cast<void*>(&HookCreateDevice));
}

static HRESULT WINAPI HookDirectInput8Create(HINSTANCE h, DWORD ver, REFIID iid, LPVOID* out, LPUNKNOWN outer) {
  if (!g_realDI8Create) return DIERR_GENERIC;
  HRESULT hr = g_realDI8Create(h, ver, iid, out, outer);
  Log("DInputFFBOnly: DirectInput8Create hr=0x%08lX obj=%p", static_cast<unsigned long>(hr), (SUCCEEDED(hr)&&out)?*out:nullptr);
  if (SUCCEEDED(hr) && out && *out && !HookDIObject(*out)) Log("DInputFFBOnly: CreateDevice hook failed");
  return hr;
}

static void CaptureNativeDInputInitBytes() {
  HMODULE exe=GetModuleHandleA(nullptr); if (!exe) return;
  BYTE* p=reinterpret_cast<BYTE*>(exe)+kDInputKillRVA;
  std::memcpy(g_originalBytes,p,kPatchSize); g_haveOriginalBytes=!IsNops(g_originalBytes);
}

static bool ActivateAfterXtendedInit() {
  std::remove("NFS_XtendedInput_DInputFFBOnly.log");
  Log("DInputFFBOnly: activating integrated Carbon filter");
  if (!g_haveOriginalBytes) { Log("DInputFFBOnly: no original bytes; keeping DInput disabled"); return false; }
  HMODULE exe=GetModuleHandleA(nullptr); if (!exe) return false;
  void* original=nullptr;
  if (!HookImport(exe,"dinput8.dll","DirectInput8Create",reinterpret_cast<void*>(&HookDirectInput8Create),&original)) {
    Log("DInputFFBOnly: IAT hook failed; keeping DInput disabled"); return false;
  }
  g_realDI8Create=reinterpret_cast<DirectInput8CreateFn>(original);
  if (!g_realDI8Create) { Log("DInputFFBOnly: null DirectInput8Create; keeping DInput disabled"); return false; }
  BYTE* p=reinterpret_cast<BYTE*>(exe)+kDInputKillRVA;
  if (!PatchBytes(p,g_originalBytes,kPatchSize)) { Log("DInputFFBOnly: failed to restore DInput init"); return false; }
  Log("DInputFFBOnly: native DInput restored; XInput stays gameplay input, DInput FFB stays available");
  return true;
}

} // namespace DInputFFBOnly
#endif
