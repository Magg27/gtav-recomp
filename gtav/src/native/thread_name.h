// gtav - name the native threads so per-thread CPU tables (bench.sh, top -H) and profilers can tell
// them apart. A std::thread inherits its creator's comm on Linux, so the renderer's record thread
// used to show up under the guest D3D thread's name ("XThread...").
#pragma once

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#endif

namespace gtav::native {
inline void NameThread(const char* name) {  // at most 15 characters on Linux
#if defined(_WIN32)
  wchar_t w[64];
  int i = 0;
  for (; name[i] && i < 63; i++) w[i] = wchar_t(name[i]);
  w[i] = 0;
  SetThreadDescription(GetCurrentThread(), w);
#elif defined(__APPLE__)
  pthread_setname_np(name);
#else
  pthread_setname_np(pthread_self(), name);
#endif
}
}  // namespace gtav::native
