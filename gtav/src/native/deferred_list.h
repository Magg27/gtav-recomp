// gtav - recording a frame on several threads (--gtav_native_record_thread,
// --gtav_native_record_workers).
//
// The backend's work per draw is mostly its own (textures, vertex conversion, constants, pipeline
// lookups), but about a fifth of the render thread went to plume and the Vulkan driver turning
// the calls into commands. DeferredCommandList takes those calls as a compact stream and other
// threads replay them into the real command lists while the backend goes on with the next draws.
// Everything the calls point at (rects, barriers, views, push constants) is copied into the
// stream; the objects themselves already live until the frame's fence.
//
// The stream of a frame is cut into chunks (Cut), each replayed into a command list of its own,
// by whichever worker is free, at the same time as the others; the lists are then submitted in
// order. What a call takes from the state of the list before it has to be set again after a cut
// (the caller cuts where a render pass begins and binds everything again). What plume keeps per
// texture and buffer for barriers is resolved when the call is made, in order (see barriers()).

#pragma once

#include <plume_render_interface.h>

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace gtav::native {

class DeferredCommandList final : public plume::RenderCommandList {
 public:
  static constexpr uint32_t kMaxWorkers = 6;
  static constexpr uint32_t kHelper = kMaxWorkers;  // the "worker" index of the caller (Finish)
  static constexpr uint32_t kMaxChunks = 96;

  // on_start runs first on every worker thread (core pinning), and again before the worker's next
  // chunk after Reconfigure().
  explicit DeferredCommandList(std::function<void()> on_start = {});
  ~DeferredCommandList() override;
  void Reconfigure() { config_.fetch_add(1, std::memory_order_relaxed); }

  // Calls from now on go to `target` (begun by the caller) through `workers` threads (1 to
  // kMaxWorkers). next_list: another begun command list of the frame for Cut (nullptr: none left).
  void Begin(plume::RenderCommandList* target, uint32_t workers = 1,
             std::function<plume::RenderCommandList*()> next_list = {});
  // Cut(): the calls from here on go to a new command list (from next_list), recorded at the same
  // time as the earlier ones. Only when CanCut().
  bool CanCut() const { return !finished_ && !in_order_only_ && next_list_ && chunk_count_ < kMaxChunks; }
  void Cut();
  // Waits until every call so far is in its list; the caller records the chunks no worker has
  // started. Returns the ms that took. Calls made after it go straight into the last list.
  double Finish();
  // The frame's command lists in the order they execute: `target`, then one per cut.
  const std::vector<plume::RenderCommandList*>& Lists() const { return lists_; }
  uint32_t ChunkIndex() const { return chunk_count_ - 1; }  // the one being written
  // ms the workers (and the caller in Finish) spent replaying since the last call.
  double TakeBusyMs();

  // Records of the caller's own, between the plume calls: `fn(context, target, worker, kind,
  // payload)` runs on the thread replaying the record's chunk when the stream reaches it (the
  // backend's half-recorded draws, see Backend::Impl::DrawBack). `worker` is that thread's index
  // (kHelper: the caller of Finish): no two calls with the same index run at once. AppendUser
  // returns `size` bytes (8-byte aligned) to fill, then PublishUser() hands the record over.
  using UserFn = void (*)(void* context, plume::RenderCommandList* target, uint32_t worker, uint32_t kind,
                          const void* payload);
  void SetUser(UserFn fn, void* context) { user_fn_ = fn, user_context_ = context; }
  void* AppendUser(uint32_t kind, uint32_t size);
  void PublishUser() { Publish(); }

  void begin() override {}
  void end() override {}
  void barriers(plume::RenderBarrierStages stages, const plume::RenderBufferBarrier* bufferBarriers,
                uint32_t bufferBarriersCount, const plume::RenderTextureBarrier* textureBarriers,
                uint32_t textureBarriersCount) override;
  void dispatch(uint32_t x, uint32_t y, uint32_t z) override;
  void traceRays(uint32_t, uint32_t, uint32_t, plume::RenderBufferReference,
                 const plume::RenderShaderBindingGroupsInfo&) override;
  void drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount, uint32_t startVertexLocation,
                     uint32_t startInstanceLocation) override;
  void drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount, uint32_t startIndexLocation,
                            int32_t baseVertexLocation, uint32_t startInstanceLocation) override;
  void setPipeline(const plume::RenderPipeline* pipeline) override;
  void setComputePipelineLayout(const plume::RenderPipelineLayout* pipelineLayout) override;
  void setComputePushConstants(uint32_t rangeIndex, const void* data, uint32_t offset, uint32_t size) override;
  void setComputeDescriptorSet(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex) override;
  void setGraphicsPipelineLayout(const plume::RenderPipelineLayout* pipelineLayout) override;
  void setGraphicsPushConstants(uint32_t rangeIndex, const void* data, uint32_t offset, uint32_t size) override;
  void setGraphicsDescriptorSet(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex) override;
  void setGraphicsDescriptorSetDynamic(plume::RenderDescriptorSet* descriptorSet, uint32_t setIndex,
                                       const uint32_t* dynamicOffsets, uint32_t dynamicOffsetCount) override;
  void setGraphicsRootDescriptor(plume::RenderBufferReference bufferReference, uint32_t rootDescriptorIndex) override;
  void setRaytracingPipelineLayout(const plume::RenderPipelineLayout*) override;
  void setRaytracingPushConstants(uint32_t, const void*, uint32_t, uint32_t) override;
  void setRaytracingDescriptorSet(plume::RenderDescriptorSet*, uint32_t) override;
  void setIndexBuffer(const plume::RenderIndexBufferView* view) override;
  void setVertexBuffers(uint32_t startSlot, const plume::RenderVertexBufferView* views, uint32_t viewCount,
                        const plume::RenderInputSlot* inputSlots) override;
  void setViewports(const plume::RenderViewport* viewports, uint32_t count) override;
  void setScissors(const plume::RenderRect* scissorRects, uint32_t count) override;
  void setFramebuffer(const plume::RenderFramebuffer* framebuffer) override;
  void setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) override;
  void clearColor(uint32_t attachmentIndex, plume::RenderColor colorValue, const plume::RenderRect* clearRects,
                  uint32_t clearRectsCount) override;
  void clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue, uint32_t stencilValue,
                         const plume::RenderRect* clearRects, uint32_t clearRectsCount) override;
  void copyBufferRegion(plume::RenderBufferReference dstBuffer, plume::RenderBufferReference srcBuffer,
                        uint64_t size) override;
  void copyTextureRegion(const plume::RenderTextureCopyLocation& dstLocation,
                         const plume::RenderTextureCopyLocation& srcLocation, uint32_t dstX, uint32_t dstY,
                         uint32_t dstZ, const plume::RenderBox* srcBox) override;
  void copyBuffer(const plume::RenderBuffer* dstBuffer, const plume::RenderBuffer* srcBuffer) override;
  void copyTexture(const plume::RenderTexture* dstTexture, const plume::RenderTexture* srcTexture) override;
  void resolveTexture(const plume::RenderTexture* dstTexture, const plume::RenderTexture* srcTexture) override;
  void resolveTextureRegion(const plume::RenderTexture* dstTexture, uint32_t dstX, uint32_t dstY,
                            const plume::RenderTexture* srcTexture, const plume::RenderRect* srcRect,
                            plume::RenderResolveMode resolveMode) override;
  void buildBottomLevelAS(const plume::RenderAccelerationStructure*, plume::RenderBufferReference,
                          const plume::RenderBottomLevelASBuildInfo&) override;
  void buildTopLevelAS(const plume::RenderAccelerationStructure*, plume::RenderBufferReference,
                       plume::RenderBufferReference, const plume::RenderTopLevelASBuildInfo&) override;
  void discardTexture(const plume::RenderTexture* texture) override;
  void resetQueryPool(const plume::RenderQueryPool* queryPool, uint32_t queryFirstIndex, uint32_t queryCount) override;
  void writeTimestamp(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void beginOcclusionQuery(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void endOcclusionQuery(const plume::RenderQueryPool* queryPool, uint32_t queryIndex) override;
  void setFragmentShadingRate(uint32_t width, uint32_t height) override;

 private:
  enum class Op : uint16_t;
  struct Header {
    Op op;
    uint16_t pad;
    uint32_t size;  // of the whole record, header included, a multiple of 8
  };
  static constexpr uint32_t kBlockSize = 1u << 20;
  static constexpr uint32_t kMaxBlocks = 256;
  // A run of records replayed into one command list. Written by the producer (the thread making
  // the calls); read by the one thread that took it.
  struct Chunk {
    plume::RenderCommandList* list = nullptr;
    const uint8_t* start = nullptr;
    std::atomic<uint64_t> published{0};  // records
    std::atomic<bool> closed{false};     // `published` is final
  };

  // A record of `payload` bytes; Publish() after filling it.
  uint8_t* Append(Op op, uint32_t payload);
  void Publish();
  template <class T>
  static uint32_t Bytes(uint32_t count) {
    return uint32_t(sizeof(T)) * count;
  }
  plume::RenderTextureLayout Tracked(const plume::RenderTexture* texture) {
    return texture ? target_->trackedLayout(texture) : plume::RenderTextureLayout::UNKNOWN;
  }
  void OpenChunk(plume::RenderCommandList* list);  // mutex_ held
  Chunk* Take(uint32_t worker);                    // mutex_ held
  void Run(uint32_t worker);
  void ReplayChunk(Chunk& chunk, uint32_t worker);
  void Replay(const Header* h, plume::RenderCommandList* t, uint32_t worker);

  plume::RenderCommandList* target_ = nullptr;
  std::function<plume::RenderCommandList*()> next_list_;
  std::vector<plume::RenderCommandList*> lists_;
  UserFn user_fn_ = nullptr;
  void* user_context_ = nullptr;
  // Producer side.
  std::vector<std::unique_ptr<uint8_t[]>> blocks_;  // a frame starts at the first one again
  uint32_t write_block_ = 0, write_offset_ = 0;
  Header* last_ = nullptr;      // the record being filled
  uint32_t last_offset_ = 0;    // where it starts
  Chunk* open_ = nullptr;       // the chunk being written
  uint64_t open_records_ = 0;
  uint64_t since_wake_ = 0;
  bool finished_ = true;        // between Finish and Begin: calls replay at once
  bool in_order_only_ = false;  // the backend resolves barriers only as they are recorded: no cuts
  // Shared.
  std::array<Chunk, kMaxChunks> chunks_;
  std::atomic<uint64_t> busy_ns_{0};
  std::atomic<uint32_t> sleepers_{0};
  std::atomic<uint32_t> config_{0};

  std::function<void()> on_start_;
  std::vector<std::thread> threads_;
  std::mutex mutex_;  // the fields below, and the sleeps
  std::condition_variable work_cv_, done_cv_;
  uint32_t active_workers_ = 1;
  uint32_t chunk_count_ = 0, next_take_ = 0, done_count_ = 0;
  bool stop_ = false;
};

}  // namespace gtav::native
