#pragma once

#include <windows.h>
#include <SpecialK/hooks.h>

// Function pointer to the real QPC so we can call it inside our hook
typedef BOOL(WINAPI* QPC_T)(LARGE_INTEGER* lpPerformanceCount);
static QPC_T fpQPC = nullptr;

// Variables to track our fake time
//base multiplier
static float g_SpeedMultiplierBase = 1.0f;
//current multiplier
static float g_SpeedMultiplier = 1.0f;
static LARGE_INTEGER g_FakeTime = { 0 };
static LARGE_INTEGER g_RealPrevTime = { 0 };

// Our detour function - the game calls this instead of the real QPC
static BOOL WINAPI DetourQPC(LARGE_INTEGER* lpPerformanceCount) {
  LARGE_INTEGER realCurrentTime;

  // 1. Get the actual time from the hardware via the original function
  if (fpQPC == nullptr) {
    // fallback: try to call the real API directly
    typedef BOOL(WINAPI* QPC_RAW_T)(LARGE_INTEGER*);
    auto proc = (QPC_RAW_T)GetProcAddress(GetModuleHandleW(L"KernelBase"), "QueryPerformanceCounter");
    if (proc == nullptr) return FALSE;
    if (!proc(&realCurrentTime)) return FALSE;
  }
  else {
    if (!fpQPC(&realCurrentTime)) return FALSE;
  }

  // 2. Initialize on first run
  if (g_RealPrevTime.QuadPart == 0) {
    g_RealPrevTime = realCurrentTime;
    g_FakeTime = realCurrentTime;
  }

  // 3. Calculate how much time actually passed
  long long delta = realCurrentTime.QuadPart - g_RealPrevTime.QuadPart;

  // 4. Apply the multiplier to the delta and add to our fake timeline
  g_FakeTime.QuadPart += (long long)(delta * g_SpeedMultiplier);

  // 5. Update previous real time for the next call
  g_RealPrevTime = realCurrentTime;

  // 6. Give the game our "doctored" time
  lpPerformanceCount->QuadPart = g_FakeTime.QuadPart;

  return TRUE;
}

// Initialize hook using Special K helpers
inline std::string InitializeSpeedHook(float factor=1.0)
{
  g_SpeedMultiplier = factor;

  // Create a DLL hook for QueryPerformanceCounter in KernelBase
  MH_STATUS st = SK_CreateDLLHook(L"Kernel32", "QueryPerformanceCounter", (void*)DetourQPC, (void**)&fpQPC);
  if (st != MH_OK) {
    // try Kernel32 as a fallback
    st = SK_CreateDLLHook(L"KernelBase", "QueryPerformanceCounter", (void*)DetourQPC, (void**)&fpQPC);
    if (st != MH_OK)
      return "Failed to create hook for QueryPerformanceCounter in KernelBase or Kernel32";
  }

  
  if(SK_ApplyQueuedHooks() == MH_OK)
    return "Speed hook initialized successfully";
  return "Failed to apply queued hooks for speed hack";
}

inline void UninitializeSpeedHook()
{
  // Disable the hook if we have the original
  if (fpQPC) {
    SK_DisableHook((void*)fpQPC);
  }
  SK_MinHook_UnInit();
}

inline void SetSpeedMultiplier(float factor)
{
  g_SpeedMultiplierBase = factor;
}


struct TemporarySpedup
{
  float factor;
  float duration_ms;
  LONGLONG startTime;
};

std::optional<TemporarySpedup> g_TemporarySpeedup;

LARGE_INTEGER get_real_time()
{
  LARGE_INTEGER realCurrentTime;
  if (fpQPC == nullptr)
  {
    QueryPerformanceCounter(&realCurrentTime);
  }
  else
  {
    fpQPC(&realCurrentTime);
  }
  return realCurrentTime;
}

void ApplyTemporarySpedup(float factor, float duration_ms)
{
  LARGE_INTEGER realCurrentTime = get_real_time();

  g_TemporarySpeedup = TemporarySpedup{ factor, duration_ms, realCurrentTime.QuadPart };
  g_SpeedMultiplier = g_SpeedMultiplierBase * factor;
}


void TimeOnFrameUpdate()
{
  if (g_TemporarySpeedup.has_value())
  {
    LARGE_INTEGER realCurrentTime = get_real_time();
    const auto& spedup = g_TemporarySpeedup.value();
    LONGLONG elapsedTicks = realCurrentTime.QuadPart - spedup.startTime;
    LARGE_INTEGER freq = { 0 };
    QueryPerformanceFrequency(&freq);
    double elapsed_ms = (double)elapsedTicks * 1000.0 / (double)freq.QuadPart;
    if (elapsed_ms < spedup.duration_ms)
    {
      g_SpeedMultiplier = g_SpeedMultiplierBase * spedup.factor;
    }
    else
    {
      g_TemporarySpeedup.reset();
      g_SpeedMultiplier = g_SpeedMultiplierBase;
    }
  }
  else
  {
    g_SpeedMultiplier = g_SpeedMultiplierBase;
  }
}