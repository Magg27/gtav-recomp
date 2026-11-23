// gtav - recording a frame on two threads (see deferred_list.h).

#include "deferred_list.h"
#include "thread_name.h"

#include <rex/logging.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <type_traits>

namespace gtav::native {

using namespace plume;

enum class DeferredCommandList::Op : uint16_t {
  NextBlock,
  Barriers,
  Dispatch,
  DrawInstanced,
  DrawIndexedInstanced,
  SetPipeline,
  SetComputePipelineLayout,
  SetComputePushConstants,
  SetComputeDescriptorSet,
  SetGraphicsPipelineLayout,
  SetGraphicsPushConstants,
  SetGraphicsDescriptorSet,
  SetGraphicsDescriptorSetDynamic,
  SetGraphicsRootDescriptor,
  SetIndexBuffer,
  SetVertexBuffers,
  SetViewports,
  SetScissors,
  SetFramebuffer,
  SetDepthBias,
  ClearColor,
  ClearDepthStencil,
  CopyBufferRegion,
  CopyTextureRegion,
  CopyBuffer,
  CopyTexture,
  ResolveTexture,
  ResolveTextureRegion,
  DiscardTexture,
  ResetQueryPool,
  WriteTimestamp,
  BeginOcclusionQuery,
  EndOcclusionQuery,
  SetFragmentShadingRate,
  User,  // Header::pad: the caller's kind
};

namespace {
// Pending records before the producer wakes a sleeping replay thread (a wake per record would cost
// more than the replay saves).
constexpr uint64_t kWakeBatch = 48;
constexpr uint32_t Align8(uint32_t n) { return (n + 7) & ~7u; }

// Payload layouts.
// Followed by the buffer barriers, the texture barriers and, resolved (see barriers()), the state
// each resource was in before: stages per buffer, layout per texture, stages per texture.
struct BarriersRec {
  RenderBarrierStages stages;
  uint32_t buffers, textures;
  uint32_t resolved;
};
struct DrawRec {
  uint32_t a, b, c;
  int32_t d;
  uint32_t e;
};
struct PtrRec {
  const void* p;
  uint32_t index;
};
struct PushRec {
  uint32_t range, offset, size;
};
struct DynamicSetRec {
  RenderDescriptorSet* set;
  uint32_t index, count;
};
struct RootRec {
  RenderBufferReference ref;
  uint32_t index;
};
struct IndexRec {
  RenderIndexBufferView view;
  bool null;
};
struct VertexRec {
  uint32_t start, count;
  bool slots;
};
struct ClearColorRec {
  uint32_t index;
  RenderColor color;
  uint32_t rects;
};
struct ClearDepthRec {
  bool depth, stencil;
  float value;
  uint32_t stencil_value, rects;
};
struct CopyRegionRec {
  RenderBufferReference dst, src;
  uint64_t size;
};
// dst_layout / src_layout (texture copies and resolves): the layouts plume kept for the textures
// when the call was made (RenderCommandList::trackedLayout), for overrideNextCopyLayouts.
struct CopyTextureRegionRec {
  RenderTextureCopyLocation dst, src;
  uint32_t x, y, z;
  bool box;
  RenderBox src_box;
  RenderTextureLayout dst_layout, src_layout;
};
struct PairRec {
  const void* dst;
  const void* src;
  RenderTextureLayout dst_layout, src_layout;
};
struct ResolveRegionRec {
  const RenderTexture* dst;
  uint32_t x, y;
  const RenderTexture* src;
  bool rect;
  RenderRect src_rect;
  RenderResolveMode mode;
  RenderTextureLayout dst_layout, src_layout;
};
struct QueryRec {
  const RenderQueryPool* pool;
  uint32_t first, count;
};
}  // namespace

DeferredCommandList::DeferredCommandList(std::function<void()> on_start) : on_start_(std::move(on_start)) {}

DeferredCommandList::~DeferredCommandList() {
  {
    std::lock_guard lock(mutex_);
    stop_ = true;
  }
  work_cv_.notify_all();
  for (auto& t : threads_) t.join();
}

void DeferredCommandList::Begin(RenderCommandList* target, uint32_t workers,
                                std::function<RenderCommandList*()> next_list) {
  std::lock_guard lock(mutex_);  // the workers are idle: every chunk of the last frame was replayed
  target_ = target;
  next_list_ = std::move(next_list);
  active_workers_ = std::clamp(workers, 1u, kMaxWorkers);
  while (threads_.size() < active_workers_)
    threads_.emplace_back([this, worker = uint32_t(threads_.size())] { Run(worker); });
  if (blocks_.empty()) blocks_.push_back(std::make_unique<uint8_t[]>(kBlockSize));
  write_block_ = write_offset_ = 0;
  chunk_count_ = next_take_ = done_count_ = 0;
  since_wake_ = 0;
  lists_.clear();
  finished_ = false;
  OpenChunk(target);
}

void DeferredCommandList::OpenChunk(RenderCommandList* list) {
  Chunk& c = chunks_[chunk_count_];
  c.list = list;
  c.start = blocks_[write_block_].get() + write_offset_;
  c.published.store(0, std::memory_order_relaxed);
  c.closed.store(false, std::memory_order_relaxed);
  open_ = &c;
  open_records_ = 0;
  lists_.push_back(list);
  chunk_count_++;
}

void DeferredCommandList::Cut() {
  RenderCommandList* list = next_list_();
  open_->closed.store(true, std::memory_order_release);
  {
    std::lock_guard lock(mutex_);
    OpenChunk(list);
  }
  // The reader of the closed chunk may sleep for more of it, and an idle worker can take the new one.
  if (sleepers_.load(std::memory_order_seq_cst)) {
    std::lock_guard lock(mutex_);
    work_cv_.notify_all();
  }
}

DeferredCommandList::Chunk* DeferredCommandList::Take(uint32_t worker) {
  if (next_take_ == chunk_count_ || (worker != kHelper && worker >= active_workers_)) return nullptr;
  return &chunks_[next_take_++];
}

double DeferredCommandList::Finish() {
  if (finished_) return 0.0;
  auto start = std::chrono::steady_clock::now();
  open_->closed.store(true, std::memory_order_release);
  finished_ = true;
  for (;;) {  // what no worker has started (all of a small frame: no thread is woken for it)
    Chunk* c;
    {
      std::lock_guard lock(mutex_);
      c = Take(kHelper);
    }
    if (!c) break;
    ReplayChunk(*c, kHelper);
  }
  {
    std::unique_lock lock(mutex_);
    if (done_count_ != chunk_count_) work_cv_.notify_all();  // (a reader sleeping for more of its chunk)
    while (done_count_ != chunk_count_) done_cv_.wait_for(lock, std::chrono::milliseconds(1));
  }
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

double DeferredCommandList::TakeBusyMs() { return double(busy_ns_.exchange(0)) * 1e-6; }

uint8_t* DeferredCommandList::Append(Op op, uint32_t payload) {
  // A block ends with a NextBlock record: a header and the address of the next block.
  constexpr uint32_t kNext = uint32_t(sizeof(Header) + sizeof(uint8_t*));
  const uint32_t size = Align8(uint32_t(sizeof(Header)) + payload);
  if (size + kNext > kBlockSize) {
    REXLOG_ERROR("[native] deferred command of {} bytes", size);
    std::abort();
  }
  if (write_offset_ + size + kNext > kBlockSize) {
    if (write_block_ + 1 == blocks_.size()) {
      if (blocks_.size() == kMaxBlocks) {
        REXLOG_ERROR("[native] deferred commands of one frame past {} MB", kMaxBlocks * (kBlockSize >> 20));
        std::abort();
      }
      blocks_.push_back(std::make_unique<uint8_t[]>(kBlockSize));
    }
    if (!finished_) {
      auto* h = reinterpret_cast<Header*>(blocks_[write_block_].get() + write_offset_);
      h->op = Op::NextBlock;
      h->size = kNext;
      const uint8_t* next = blocks_[write_block_ + 1].get();
      std::memcpy(h + 1, &next, sizeof(next));
      open_->published.store(++open_records_, std::memory_order_release);
    }
    write_block_++;
    write_offset_ = 0;
  }
  auto* h = reinterpret_cast<Header*>(blocks_[write_block_].get() + write_offset_);
  h->op = op;
  h->size = size;
  last_ = h;
  last_offset_ = write_offset_;
  write_offset_ += size;
  return reinterpret_cast<uint8_t*>(h + 1);
}

void* DeferredCommandList::AppendUser(uint32_t kind, uint32_t size) {
  uint8_t* p = Append(Op::User, size);
  (reinterpret_cast<Header*>(p) - 1)->pad = uint16_t(kind);
  return p;
}

void DeferredCommandList::Publish() {
  if (finished_) {
    // After Finish: every list is complete up to here and no worker is in one.
    Replay(last_, lists_.back(), kHelper);
    write_offset_ = last_offset_;
    return;
  }
  open_->published.store(++open_records_, std::memory_order_release);
  if (++since_wake_ >= kWakeBatch) {
    since_wake_ = 0;
    if (sleepers_.load(std::memory_order_seq_cst)) {
      std::lock_guard lock(mutex_);
      work_cv_.notify_all();
    }
  }
}

void DeferredCommandList::Run(uint32_t worker) {
  char name[16];
  std::snprintf(name, sizeof(name), "gtav-rec%u", worker + 1);
  NameThread(name);
  uint32_t config = config_.load(std::memory_order_relaxed);
  if (on_start_) on_start_();
  for (;;) {
    Chunk* c;
    {
      std::unique_lock lock(mutex_);
      for (;;) {
        if (stop_) return;
        if ((c = Take(worker))) break;
        // Until the producer has a batch of records or a new chunk (it notifies under the mutex
        // when sleepers_ is set, and a chunk opens under the mutex: no wake is missed).
        sleepers_.fetch_add(1, std::memory_order_seq_cst);
        work_cv_.wait(lock);
        sleepers_.fetch_sub(1, std::memory_order_seq_cst);
      }
    }
    if (const uint32_t now = config_.load(std::memory_order_relaxed); now != config) {
      config = now;
      if (on_start_) on_start_();
    }
    ReplayChunk(*c, worker);
  }
}

void DeferredCommandList::ReplayChunk(Chunk& chunk, uint32_t worker) {
  const uint8_t* at = chunk.start;
  uint64_t done = 0;
  for (;;) {
    // `closed` first: once it is set, `published` is final.
    const bool closed = chunk.closed.load(std::memory_order_acquire);
    uint64_t available = chunk.published.load(std::memory_order_acquire);
    if (available == done) {
      if (closed) break;
      for (int i = 0; i < 64 && available == done; i++) {
        std::this_thread::yield();
        available = chunk.published.load(std::memory_order_acquire);
      }
      if (available == done) {
        // Sleep until the producer has a batch (it wakes sleepers every kWakeBatch records, and at
        // a cut and in Finish) or the timeout.
        std::unique_lock lock(mutex_);
        sleepers_.fetch_add(1, std::memory_order_seq_cst);
        if (chunk.published.load(std::memory_order_seq_cst) == done && !chunk.closed.load(std::memory_order_seq_cst))
          work_cv_.wait_for(lock, std::chrono::milliseconds(2));
        sleepers_.fetch_sub(1, std::memory_order_seq_cst);
        continue;
      }
    }
    auto start = std::chrono::steady_clock::now();
    while (done < available) {
      auto* h = reinterpret_cast<const Header*>(at);
      if (h->op == Op::NextBlock) {
        std::memcpy(&at, h + 1, sizeof(at));
      } else {
        Replay(h, chunk.list, worker);
        at += h->size;
      }
      done++;
    }
    busy_ns_.fetch_add(uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                    std::chrono::steady_clock::now() - start).count()),
                       std::memory_order_relaxed);
  }
  std::lock_guard lock(mutex_);
  done_count_++;
  done_cv_.notify_all();
}

void DeferredCommandList::Replay(const Header* h, RenderCommandList* t, uint32_t worker) {
  const uint8_t* p = reinterpret_cast<const uint8_t*>(h + 1);
  auto rec = [&](auto* type) -> decltype(auto) {
    using T = std::remove_pointer_t<decltype(type)>;
    return *reinterpret_cast<const T*>(p);
  };
  auto after = [&](auto* type) {
    using T = std::remove_pointer_t<decltype(type)>;
    return p + Align8(sizeof(T));
  };
  switch (h->op) {
    case Op::NextBlock:
      break;
    case Op::Barriers: {
      const auto& r = rec((BarriersRec*)nullptr);
      auto* buffers = reinterpret_cast<const RenderBufferBarrier*>(after((BarriersRec*)nullptr));
      auto* textures = reinterpret_cast<const RenderTextureBarrier*>(
          reinterpret_cast<const uint8_t*>(buffers) + Align8(Bytes<RenderBufferBarrier>(r.buffers)));
      if (!r.resolved) {
        t->barriers(r.stages, r.buffers ? buffers : nullptr, r.buffers, r.textures ? textures : nullptr, r.textures);
        break;
      }
      auto* buffer_stages = reinterpret_cast<const RenderBarrierStages*>(
          reinterpret_cast<const uint8_t*>(textures) + Align8(Bytes<RenderTextureBarrier>(r.textures)));
      auto* texture_layouts = reinterpret_cast<const RenderTextureLayout*>(
          reinterpret_cast<const uint8_t*>(buffer_stages) + Align8(Bytes<RenderBarrierStages>(r.buffers)));
      auto* texture_stages = reinterpret_cast<const RenderBarrierStages*>(
          reinterpret_cast<const uint8_t*>(texture_layouts) + Align8(Bytes<RenderTextureLayout>(r.textures)));
      t->barriersResolved(r.stages, r.buffers ? buffers : nullptr, r.buffers, buffer_stages,
                          r.textures ? textures : nullptr, r.textures, texture_layouts, texture_stages);
      break;
    }
    case Op::Dispatch: {
      const auto& r = rec((DrawRec*)nullptr);
      t->dispatch(r.a, r.b, r.c);
      break;
    }
    case Op::DrawInstanced: {
      const auto& r = rec((DrawRec*)nullptr);
      t->drawInstanced(r.a, r.b, r.c, r.e);
      break;
    }
    case Op::DrawIndexedInstanced: {
      const auto& r = rec((DrawRec*)nullptr);
      t->drawIndexedInstanced(r.a, r.b, r.c, r.d, r.e);
      break;
    }
    case Op::SetPipeline:
      t->setPipeline(static_cast<const RenderPipeline*>(rec((PtrRec*)nullptr).p));
      break;
    case Op::SetComputePipelineLayout:
      t->setComputePipelineLayout(static_cast<const RenderPipelineLayout*>(rec((PtrRec*)nullptr).p));
      break;
    case Op::SetGraphicsPipelineLayout:
      t->setGraphicsPipelineLayout(static_cast<const RenderPipelineLayout*>(rec((PtrRec*)nullptr).p));
      break;
    case Op::SetComputePushConstants:
    case Op::SetGraphicsPushConstants: {
      const auto& r = rec((PushRec*)nullptr);
      const void* data = after((PushRec*)nullptr);
      if (h->op == Op::SetComputePushConstants)
        t->setComputePushConstants(r.range, data, r.offset, r.size);
      else
        t->setGraphicsPushConstants(r.range, data, r.offset, r.size);
      break;
    }
    case Op::SetComputeDescriptorSet: {
      const auto& r = rec((PtrRec*)nullptr);
      t->setComputeDescriptorSet(static_cast<RenderDescriptorSet*>(const_cast<void*>(r.p)), r.index);
      break;
    }
    case Op::SetGraphicsDescriptorSet: {
      const auto& r = rec((PtrRec*)nullptr);
      t->setGraphicsDescriptorSet(static_cast<RenderDescriptorSet*>(const_cast<void*>(r.p)), r.index);
      break;
    }
    case Op::SetGraphicsDescriptorSetDynamic: {
      const auto& r = rec((DynamicSetRec*)nullptr);
      t->setGraphicsDescriptorSetDynamic(r.set, r.index,
                                         reinterpret_cast<const uint32_t*>(after((DynamicSetRec*)nullptr)), r.count);
      break;
    }
    case Op::SetGraphicsRootDescriptor: {
      const auto& r = rec((RootRec*)nullptr);
      t->setGraphicsRootDescriptor(r.ref, r.index);
      break;
    }
    case Op::SetIndexBuffer: {
      const auto& r = rec((IndexRec*)nullptr);
      t->setIndexBuffer(r.null ? nullptr : &r.view);
      break;
    }
    case Op::SetVertexBuffers: {
      const auto& r = rec((VertexRec*)nullptr);
      auto* views = reinterpret_cast<const RenderVertexBufferView*>(after((VertexRec*)nullptr));
      auto* slots = reinterpret_cast<const RenderInputSlot*>(reinterpret_cast<const uint8_t*>(views) +
                                                             Align8(Bytes<RenderVertexBufferView>(r.count)));
      t->setVertexBuffers(r.start, views, r.count, r.slots ? slots : nullptr);
      break;
    }
    case Op::SetViewports: {
      const uint32_t n = rec((uint32_t*)nullptr);
      t->setViewports(reinterpret_cast<const RenderViewport*>(p + 8), n);
      break;
    }
    case Op::SetScissors: {
      const uint32_t n = rec((uint32_t*)nullptr);
      t->setScissors(reinterpret_cast<const RenderRect*>(p + 8), n);
      break;
    }
    case Op::SetFramebuffer:
      t->setFramebuffer(static_cast<const RenderFramebuffer*>(rec((PtrRec*)nullptr).p));
      break;
    case Op::SetDepthBias: {
      auto* f = reinterpret_cast<const float*>(p);
      t->setDepthBias(f[0], f[1], f[2]);
      break;
    }
    case Op::ClearColor: {
      const auto& r = rec((ClearColorRec*)nullptr);
      auto* rects = reinterpret_cast<const RenderRect*>(after((ClearColorRec*)nullptr));
      t->clearColor(r.index, r.color, r.rects ? rects : nullptr, r.rects);
      break;
    }
    case Op::ClearDepthStencil: {
      const auto& r = rec((ClearDepthRec*)nullptr);
      auto* rects = reinterpret_cast<const RenderRect*>(after((ClearDepthRec*)nullptr));
      t->clearDepthStencil(r.depth, r.stencil, r.value, r.stencil_value, r.rects ? rects : nullptr, r.rects);
      break;
    }
    case Op::CopyBufferRegion: {
      const auto& r = rec((CopyRegionRec*)nullptr);
      t->copyBufferRegion(r.dst, r.src, r.size);
      break;
    }
    case Op::CopyTextureRegion: {
      const auto& r = rec((CopyTextureRegionRec*)nullptr);
      t->overrideNextCopyLayouts(r.dst_layout, r.src_layout);
      t->copyTextureRegion(r.dst, r.src, r.x, r.y, r.z, r.box ? &r.src_box : nullptr);
      break;
    }
    case Op::CopyBuffer: {
      const auto& r = rec((PairRec*)nullptr);
      t->copyBuffer(static_cast<const RenderBuffer*>(r.dst), static_cast<const RenderBuffer*>(r.src));
      break;
    }
    case Op::CopyTexture: {
      const auto& r = rec((PairRec*)nullptr);
      t->overrideNextCopyLayouts(r.dst_layout, r.src_layout);
      t->copyTexture(static_cast<const RenderTexture*>(r.dst), static_cast<const RenderTexture*>(r.src));
      break;
    }
    case Op::ResolveTexture: {
      const auto& r = rec((PairRec*)nullptr);
      t->overrideNextCopyLayouts(r.dst_layout, r.src_layout);
      t->resolveTexture(static_cast<const RenderTexture*>(r.dst), static_cast<const RenderTexture*>(r.src));
      break;
    }
    case Op::ResolveTextureRegion: {
      const auto& r = rec((ResolveRegionRec*)nullptr);
      t->overrideNextCopyLayouts(r.dst_layout, r.src_layout);
      t->resolveTextureRegion(r.dst, r.x, r.y, r.src, r.rect ? &r.src_rect : nullptr, r.mode);
      break;
    }
    case Op::DiscardTexture:
      t->discardTexture(static_cast<const RenderTexture*>(rec((PtrRec*)nullptr).p));
      break;
    case Op::ResetQueryPool: {
      const auto& r = rec((QueryRec*)nullptr);
      t->resetQueryPool(r.pool, r.first, r.count);
      break;
    }
    case Op::WriteTimestamp: {
      const auto& r = rec((QueryRec*)nullptr);
      t->writeTimestamp(r.pool, r.first);
      break;
    }
    case Op::BeginOcclusionQuery: {
      const auto& r = rec((QueryRec*)nullptr);
      t->beginOcclusionQuery(r.pool, r.first);
      break;
    }
    case Op::EndOcclusionQuery: {
      const auto& r = rec((QueryRec*)nullptr);
      t->endOcclusionQuery(r.pool, r.first);
      break;
    }
    case Op::SetFragmentShadingRate: {
      const auto& r = rec((DrawRec*)nullptr);
      t->setFragmentShadingRate(r.a, r.b);
      break;
    }
    case Op::User:
      user_fn_(user_context_, t, worker, h->pad, p);
      break;
  }
}

// --- the calls ---

namespace {
template <class T>
void Put(uint8_t*& p, const T& v) {
  std::memcpy(p, &v, sizeof(T));
  p += Align8(sizeof(T));
}
template <class T>
void PutArray(uint8_t*& p, const T* v, uint32_t n) {
  if (n) std::memcpy(p, v, sizeof(T) * n);
  p += Align8(uint32_t(sizeof(T)) * n);
}
}  // namespace

void DeferredCommandList::barriers(RenderBarrierStages stages, const RenderBufferBarrier* bufferBarriers,
                                   uint32_t bufferBarriersCount, const RenderTextureBarrier* textureBarriers,
                                   uint32_t textureBarriersCount) {
  const uint32_t nb = bufferBarriersCount, nt = textureBarriersCount;
  uint8_t* p = Append(Op::Barriers, Align8(sizeof(BarriersRec)) + Align8(Bytes<RenderBufferBarrier>(nb)) +
                                        Align8(Bytes<RenderTextureBarrier>(nt)) + Align8(Bytes<RenderBarrierStages>(nb)) +
                                        Align8(Bytes<RenderTextureLayout>(nt)) + Align8(Bytes<RenderBarrierStages>(nt)));
  auto* rec = reinterpret_cast<BarriersRec*>(p);
  Put(p, BarriersRec{stages, nb, nt, 0});
  PutArray(p, bufferBarriers, nb);
  PutArray(p, textureBarriers, nt);
  auto* buffer_stages = reinterpret_cast<RenderBarrierStages*>(p);
  auto* texture_layouts = reinterpret_cast<RenderTextureLayout*>(p + Align8(Bytes<RenderBarrierStages>(nb)));
  auto* texture_stages = reinterpret_cast<RenderBarrierStages*>(reinterpret_cast<uint8_t*>(texture_layouts) +
                                                                Align8(Bytes<RenderTextureLayout>(nt)));
  // The state plume keeps per resource moves on here, in the order of the frame; the barrier itself
  // is recorded from the copy of the old state whenever (and on whichever list) the record plays.
  rec->resolved = target_->resolveBarriers(stages, bufferBarriers, nb, textureBarriers, nt, buffer_stages,
                                           texture_layouts, texture_stages);
  if (!rec->resolved) in_order_only_ = true;
  Publish();
}

void DeferredCommandList::dispatch(uint32_t x, uint32_t y, uint32_t z) {
  uint8_t* p = Append(Op::Dispatch, sizeof(DrawRec));
  Put(p, DrawRec{x, y, z, 0, 0});
  Publish();
}

void DeferredCommandList::traceRays(uint32_t, uint32_t, uint32_t, RenderBufferReference,
                                    const RenderShaderBindingGroupsInfo&) {
  std::abort();  // not used by the backend
}

void DeferredCommandList::drawInstanced(uint32_t vertexCountPerInstance, uint32_t instanceCount,
                                        uint32_t startVertexLocation, uint32_t startInstanceLocation) {
  uint8_t* p = Append(Op::DrawInstanced, sizeof(DrawRec));
  Put(p, DrawRec{vertexCountPerInstance, instanceCount, startVertexLocation, 0, startInstanceLocation});
  Publish();
}

void DeferredCommandList::drawIndexedInstanced(uint32_t indexCountPerInstance, uint32_t instanceCount,
                                               uint32_t startIndexLocation, int32_t baseVertexLocation,
                                               uint32_t startInstanceLocation) {
  uint8_t* p = Append(Op::DrawIndexedInstanced, sizeof(DrawRec));
  Put(p, DrawRec{indexCountPerInstance, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation});
  Publish();
}

#define GTAV_DEFERRED_PTR(method, op, type)            \
  void DeferredCommandList::method(type value) {       \
    uint8_t* p = Append(Op::op, sizeof(PtrRec));       \
    Put(p, PtrRec{value, 0});                          \
    Publish();                                         \
  }
GTAV_DEFERRED_PTR(setPipeline, SetPipeline, const RenderPipeline*)
GTAV_DEFERRED_PTR(setComputePipelineLayout, SetComputePipelineLayout, const RenderPipelineLayout*)
GTAV_DEFERRED_PTR(setGraphicsPipelineLayout, SetGraphicsPipelineLayout, const RenderPipelineLayout*)
GTAV_DEFERRED_PTR(setFramebuffer, SetFramebuffer, const RenderFramebuffer*)
GTAV_DEFERRED_PTR(discardTexture, DiscardTexture, const RenderTexture*)
#undef GTAV_DEFERRED_PTR

void DeferredCommandList::setComputePushConstants(uint32_t rangeIndex, const void* data, uint32_t offset,
                                                  uint32_t size) {
  if (!size) std::abort();  // the backend always passes the size (the range's is not known here)
  uint8_t* p = Append(Op::SetComputePushConstants, Align8(sizeof(PushRec)) + size);
  Put(p, PushRec{rangeIndex, offset, size});
  std::memcpy(p, data, size);
  Publish();
}

void DeferredCommandList::setGraphicsPushConstants(uint32_t rangeIndex, const void* data, uint32_t offset,
                                                   uint32_t size) {
  if (!size) std::abort();
  uint8_t* p = Append(Op::SetGraphicsPushConstants, Align8(sizeof(PushRec)) + size);
  Put(p, PushRec{rangeIndex, offset, size});
  std::memcpy(p, data, size);
  Publish();
}

void DeferredCommandList::setComputeDescriptorSet(RenderDescriptorSet* descriptorSet, uint32_t setIndex) {
  uint8_t* p = Append(Op::SetComputeDescriptorSet, sizeof(PtrRec));
  Put(p, PtrRec{descriptorSet, setIndex});
  Publish();
}

void DeferredCommandList::setGraphicsDescriptorSet(RenderDescriptorSet* descriptorSet, uint32_t setIndex) {
  uint8_t* p = Append(Op::SetGraphicsDescriptorSet, sizeof(PtrRec));
  Put(p, PtrRec{descriptorSet, setIndex});
  Publish();
}

void DeferredCommandList::setGraphicsDescriptorSetDynamic(RenderDescriptorSet* descriptorSet, uint32_t setIndex,
                                                          const uint32_t* dynamicOffsets, uint32_t dynamicOffsetCount) {
  uint8_t* p = Append(Op::SetGraphicsDescriptorSetDynamic,
                      Align8(sizeof(DynamicSetRec)) + Align8(Bytes<uint32_t>(dynamicOffsetCount)));
  Put(p, DynamicSetRec{descriptorSet, setIndex, dynamicOffsetCount});
  PutArray(p, dynamicOffsets, dynamicOffsetCount);
  Publish();
}

void DeferredCommandList::setGraphicsRootDescriptor(RenderBufferReference bufferReference,
                                                    uint32_t rootDescriptorIndex) {
  uint8_t* p = Append(Op::SetGraphicsRootDescriptor, sizeof(RootRec));
  Put(p, RootRec{bufferReference, rootDescriptorIndex});
  Publish();
}

void DeferredCommandList::setRaytracingPipelineLayout(const RenderPipelineLayout*) { std::abort(); }
void DeferredCommandList::setRaytracingPushConstants(uint32_t, const void*, uint32_t, uint32_t) { std::abort(); }
void DeferredCommandList::setRaytracingDescriptorSet(RenderDescriptorSet*, uint32_t) { std::abort(); }

void DeferredCommandList::setIndexBuffer(const RenderIndexBufferView* view) {
  uint8_t* p = Append(Op::SetIndexBuffer, sizeof(IndexRec));
  IndexRec r{};
  r.null = view == nullptr;
  if (view) r.view = *view;
  Put(p, r);
  Publish();
}

void DeferredCommandList::setVertexBuffers(uint32_t startSlot, const RenderVertexBufferView* views, uint32_t viewCount,
                                           const RenderInputSlot* inputSlots) {
  uint8_t* p = Append(Op::SetVertexBuffers, Align8(sizeof(VertexRec)) + Align8(Bytes<RenderVertexBufferView>(viewCount)) +
                                                Align8(Bytes<RenderInputSlot>(inputSlots ? viewCount : 0)));
  Put(p, VertexRec{startSlot, viewCount, inputSlots != nullptr});
  PutArray(p, views, viewCount);
  if (inputSlots) PutArray(p, inputSlots, viewCount);
  Publish();
}

void DeferredCommandList::setViewports(const RenderViewport* viewports, uint32_t count) {
  uint8_t* p = Append(Op::SetViewports, 8 + Align8(Bytes<RenderViewport>(count)));
  Put(p, count);
  PutArray(p, viewports, count);
  Publish();
}

void DeferredCommandList::setScissors(const RenderRect* scissorRects, uint32_t count) {
  uint8_t* p = Append(Op::SetScissors, 8 + Align8(Bytes<RenderRect>(count)));
  Put(p, count);
  PutArray(p, scissorRects, count);
  Publish();
}

void DeferredCommandList::setDepthBias(float depthBias, float depthBiasClamp, float slopeScaledDepthBias) {
  uint8_t* p = Append(Op::SetDepthBias, 12);
  const float f[3] = {depthBias, depthBiasClamp, slopeScaledDepthBias};
  std::memcpy(p, f, sizeof(f));
  Publish();
}

void DeferredCommandList::clearColor(uint32_t attachmentIndex, RenderColor colorValue, const RenderRect* clearRects,
                                     uint32_t clearRectsCount) {
  uint8_t* p = Append(Op::ClearColor, Align8(sizeof(ClearColorRec)) + Align8(Bytes<RenderRect>(clearRectsCount)));
  Put(p, ClearColorRec{attachmentIndex, colorValue, clearRectsCount});
  PutArray(p, clearRects, clearRectsCount);
  Publish();
}

void DeferredCommandList::clearDepthStencil(bool clearDepth, bool clearStencil, float depthValue,
                                            uint32_t stencilValue, const RenderRect* clearRects,
                                            uint32_t clearRectsCount) {
  uint8_t* p = Append(Op::ClearDepthStencil, Align8(sizeof(ClearDepthRec)) + Align8(Bytes<RenderRect>(clearRectsCount)));
  Put(p, ClearDepthRec{clearDepth, clearStencil, depthValue, stencilValue, clearRectsCount});
  PutArray(p, clearRects, clearRectsCount);
  Publish();
}

void DeferredCommandList::copyBufferRegion(RenderBufferReference dstBuffer, RenderBufferReference srcBuffer,
                                           uint64_t size) {
  uint8_t* p = Append(Op::CopyBufferRegion, sizeof(CopyRegionRec));
  Put(p, CopyRegionRec{dstBuffer, srcBuffer, size});
  Publish();
}

void DeferredCommandList::copyTextureRegion(const RenderTextureCopyLocation& dstLocation,
                                            const RenderTextureCopyLocation& srcLocation, uint32_t dstX, uint32_t dstY,
                                            uint32_t dstZ, const RenderBox* srcBox) {
  uint8_t* p = Append(Op::CopyTextureRegion, sizeof(CopyTextureRegionRec));
  CopyTextureRegionRec r{dstLocation, srcLocation, dstX, dstY, dstZ, srcBox != nullptr, {},
                         Tracked(dstLocation.texture), Tracked(srcLocation.texture)};
  if (srcBox) r.src_box = *srcBox;
  Put(p, r);
  Publish();
}

void DeferredCommandList::copyBuffer(const RenderBuffer* dst, const RenderBuffer* src) {
  uint8_t* p = Append(Op::CopyBuffer, sizeof(PairRec));
  Put(p, PairRec{dst, src, RenderTextureLayout::UNKNOWN, RenderTextureLayout::UNKNOWN});
  Publish();
}

void DeferredCommandList::copyTexture(const RenderTexture* dst, const RenderTexture* src) {
  uint8_t* p = Append(Op::CopyTexture, sizeof(PairRec));
  Put(p, PairRec{dst, src, Tracked(dst), Tracked(src)});
  Publish();
}

void DeferredCommandList::resolveTexture(const RenderTexture* dst, const RenderTexture* src) {
  uint8_t* p = Append(Op::ResolveTexture, sizeof(PairRec));
  Put(p, PairRec{dst, src, Tracked(dst), Tracked(src)});
  Publish();
}

void DeferredCommandList::resolveTextureRegion(const RenderTexture* dstTexture, uint32_t dstX, uint32_t dstY,
                                               const RenderTexture* srcTexture, const RenderRect* srcRect,
                                               RenderResolveMode resolveMode) {
  uint8_t* p = Append(Op::ResolveTextureRegion, sizeof(ResolveRegionRec));
  ResolveRegionRec r{dstTexture, dstX, dstY, srcTexture, srcRect != nullptr, {}, resolveMode, Tracked(dstTexture),
                     Tracked(srcTexture)};
  if (srcRect) r.src_rect = *srcRect;
  Put(p, r);
  Publish();
}

void DeferredCommandList::buildBottomLevelAS(const RenderAccelerationStructure*, RenderBufferReference,
                                             const RenderBottomLevelASBuildInfo&) {
  std::abort();
}
void DeferredCommandList::buildTopLevelAS(const RenderAccelerationStructure*, RenderBufferReference,
                                          RenderBufferReference, const RenderTopLevelASBuildInfo&) {
  std::abort();
}

void DeferredCommandList::resetQueryPool(const RenderQueryPool* queryPool, uint32_t queryFirstIndex,
                                         uint32_t queryCount) {
  uint8_t* p = Append(Op::ResetQueryPool, sizeof(QueryRec));
  Put(p, QueryRec{queryPool, queryFirstIndex, queryCount});
  Publish();
}

void DeferredCommandList::writeTimestamp(const RenderQueryPool* queryPool, uint32_t queryIndex) {
  uint8_t* p = Append(Op::WriteTimestamp, sizeof(QueryRec));
  Put(p, QueryRec{queryPool, queryIndex, 0});
  Publish();
}

void DeferredCommandList::beginOcclusionQuery(const RenderQueryPool* queryPool, uint32_t queryIndex) {
  uint8_t* p = Append(Op::BeginOcclusionQuery, sizeof(QueryRec));
  Put(p, QueryRec{queryPool, queryIndex, 0});
  Publish();
}

void DeferredCommandList::endOcclusionQuery(const RenderQueryPool* queryPool, uint32_t queryIndex) {
  uint8_t* p = Append(Op::EndOcclusionQuery, sizeof(QueryRec));
  Put(p, QueryRec{queryPool, queryIndex, 0});
  Publish();
}

void DeferredCommandList::setFragmentShadingRate(uint32_t width, uint32_t height) {
  uint8_t* p = Append(Op::SetFragmentShadingRate, sizeof(DrawRec));
  Put(p, DrawRec{width, height, 0, 0, 0});
  Publish();
}

}  // namespace gtav::native
