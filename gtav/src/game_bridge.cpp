// gtav - runtime interface for the game DLL (GTAV_GAME_DLL build, see fs/gtav_pch.h).
//
// The recompiled game code is built on the player's machine without the SDK headers, so the
// handful of runtime services the SDK's generated prefix header calls inline are exported here as
// plain C functions. Each one does what the matching macro in the SDK's gtav_pch.h does.

#include <rex/chrono/clock.h>
#include <rex/image_info.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/mmio_handler.h>
#include <rex/thread/mutex.h>

#include <atomic>
#include <cstdlib>
#include <string_view>

#include "../fs/layout_check.h"
#include "guest_irq_lock.h"

#ifdef _WIN32
#define GTAVFS_EXPORT __declspec(dllexport)
#else
// Exported from the executable's dynamic symbol table (--dynamic-list, see gen_interface.py).
#define GTAVFS_EXPORT __attribute__((visibility("default")))
#endif

namespace {
// "Interrupts off" sections of the guest (guest_irq_lock.cpp). The test answers "is this thread
// inside one": with the global lock held, the count the old code read under that lock was the
// calling thread's own depth, so the depth alone gives the same answer.
thread_local int32_t t_irq_depth = 0;   // sections this thread is inside
thread_local int32_t t_irq_locked = 0;  // of those, entered with the runtime's global lock
// --gtav_guest_irq_lock as this thread last read it (every 64th test: reading a setting from
// another module is a call, and these run about a million times a second). Every section starts
// with the test.
thread_local int32_t t_irq_mode = 1;
thread_local uint32_t t_irq_tests = 0;
}  // namespace

extern "C" {

GTAVFS_EXPORT uint32_t gtavfs_mmio_load(uint32_t addr) {
  uint32_t value = 0;
  rex::runtime::MMIOHandler::global_handler()->CheckLoad(addr, &value);
  return value;
}

GTAVFS_EXPORT void gtavfs_mmio_store(uint32_t addr, uint32_t value) {
  rex::runtime::MMIOHandler::global_handler()->CheckStore(addr, value);
}

GTAVFS_EXPORT uint64_t gtavfs_query_timebase() {
  return rex::chrono::Clock::QueryGuestTickCount();
}

GTAVFS_EXPORT PPCFunc* gtavfs_resolve_indirect(uint32_t guest_address) {
  return rex::runtime::ResolveIndirectFunction(guest_address);
}

GTAVFS_EXPORT void gtavfs_global_lock_enter() {
  if (t_irq_mode != 0) [[unlikely]] {
    rex::thread::global_critical_region::mutex().lock();
    t_irq_locked++;
  }
  t_irq_depth++;
}

GTAVFS_EXPORT void gtavfs_global_lock_leave() {
  t_irq_depth--;
  if (t_irq_locked) [[unlikely]] {  // (not the mode: it may have changed since the enter)
    t_irq_locked--;
    rex::thread::global_critical_region::mutex().unlock();
  }
}

GTAVFS_EXPORT uint64_t gtavfs_global_lock_check() {
  if ((t_irq_tests++ & 63) == 0) t_irq_mode = gtav::GuestIrqLockMode();
  if (t_irq_mode == 2) [[unlikely]] {
    // As before 11-23: wait for whoever is inside a section, then answer.
    auto lock = rex::thread::global_critical_region::AcquireDirect();
    return t_irq_depth ? 0 : 0x8000;
  }
  return t_irq_depth ? 0 : 0x8000;
}

GTAVFS_EXPORT void gtavfs_trap(PPCContext* ctx, uint8_t* base, uint16_t trap_type) {
  switch (trap_type) {
    case 20:
    case 26: {
      std::string_view str(reinterpret_cast<const char*>(base + ctx->r3.u32), ctx->r4.u16);
      REXCPU_DEBUG("(service trap) {}", str);
      break;
    }
    case 0:
    case 22:
      REXCPU_WARN("tw/td trap hit (type {})", trap_type);
      break;
    case 25:
      break;
    default:
      REXCPU_WARN("Unknown trap type {}", trap_type);
      break;
  }
}

[[noreturn]] GTAVFS_EXPORT void gtavfs_unimplemented(const char* what) {
  REXCPU_ERROR("{}", what);
  std::abort();
}

}  // extern "C"
