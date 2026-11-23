// gtav - the game blocking on a GPU fence (gpu_fence_wait.cpp).
#pragma once

namespace gtav {
// The completed-fence value in guest memory was just written (command processor thread): wakes the
// game threads sleeping in D3D's fence wait.
void FenceWritten();
}  // namespace gtav
