// gtav - the guest's "interrupts off" sections (guest_irq_lock.cpp).
#pragma once

#include <rex/cvar.h>

REXCVAR_DECLARE(int32_t, gtav_guest_irq_lock);

namespace gtav {
// 0: the game's interlocked operations take no lock; 1: the runtime's global lock; 2: as 1, and
// the "are interrupts on" test takes that lock too (the behaviour before 11-23, for comparisons).
inline int GuestIrqLockMode() {
#if defined(GTAV_GAME_DLL)
  return REXCVAR_GET(gtav_guest_irq_lock);  // the game library asks the executable: live
#elif defined(REX_GUEST_IRQ_LOCK) && !REX_GUEST_IRQ_LOCK
  return 0;  // game code inside the executable: decided by the build (generated gtav_pch.h)
#else
  return 1;
#endif
}
}  // namespace gtav
