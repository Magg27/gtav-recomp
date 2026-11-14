// gtav - inline register save/restore helpers (dev-build perf experiment).
//
// The recompiled code brackets every non-leaf guest function with the Xbox 360 CRT helpers
// __savegprlr_N / __restgprlr_N (store rN..r31 + the link register to the guest stack, load them
// back). Generated, they are extern functions: two host calls with their own prologues around
// every guest call (~2 million a second on the main thread). This header is included at the end of
// generated/default/gtav_pch.h: the helpers become always-inline functions and DEFINE_REX_FUNC gives
// their generated bodies only the __imp__ name (the trick fs/gtav_pch.h uses for hooked functions),
// so every call site inlines the stores/loads. Guest semantics are unchanged: same addresses, same
// byte-swapped volatile accesses, same lr handling. Re-apply the include after codegen regenerates
// the header. Linux/Windows x86-64 only (Mach-O has no overridable alias).
#pragma once
#if !defined(__APPLE__)
#define GTAV_GPRLR_INL___savegprlr_14 ,1
#define GTAV_GPRLR_INL___savegprlr_15 ,1
#define GTAV_GPRLR_INL___savegprlr_16 ,1
#define GTAV_GPRLR_INL___savegprlr_17 ,1
#define GTAV_GPRLR_INL___savegprlr_18 ,1
#define GTAV_GPRLR_INL___savegprlr_19 ,1
#define GTAV_GPRLR_INL___savegprlr_20 ,1
#define GTAV_GPRLR_INL___savegprlr_21 ,1
#define GTAV_GPRLR_INL___savegprlr_22 ,1
#define GTAV_GPRLR_INL___savegprlr_23 ,1
#define GTAV_GPRLR_INL___savegprlr_24 ,1
#define GTAV_GPRLR_INL___savegprlr_25 ,1
#define GTAV_GPRLR_INL___savegprlr_26 ,1
#define GTAV_GPRLR_INL___savegprlr_27 ,1
#define GTAV_GPRLR_INL___savegprlr_28 ,1
#define GTAV_GPRLR_INL___savegprlr_29 ,1
#define GTAV_GPRLR_INL___savegprlr_30 ,1
#define GTAV_GPRLR_INL___savegprlr_31 ,1
#define GTAV_GPRLR_INL___restgprlr_14 ,1
#define GTAV_GPRLR_INL___restgprlr_15 ,1
#define GTAV_GPRLR_INL___restgprlr_16 ,1
#define GTAV_GPRLR_INL___restgprlr_17 ,1
#define GTAV_GPRLR_INL___restgprlr_18 ,1
#define GTAV_GPRLR_INL___restgprlr_19 ,1
#define GTAV_GPRLR_INL___restgprlr_20 ,1
#define GTAV_GPRLR_INL___restgprlr_21 ,1
#define GTAV_GPRLR_INL___restgprlr_22 ,1
#define GTAV_GPRLR_INL___restgprlr_23 ,1
#define GTAV_GPRLR_INL___restgprlr_24 ,1
#define GTAV_GPRLR_INL___restgprlr_25 ,1
#define GTAV_GPRLR_INL___restgprlr_26 ,1
#define GTAV_GPRLR_INL___restgprlr_27 ,1
#define GTAV_GPRLR_INL___restgprlr_28 ,1
#define GTAV_GPRLR_INL___restgprlr_29 ,1
#define GTAV_GPRLR_INL___restgprlr_30 ,1
#define GTAV_GPRLR_INL___restgprlr_31 ,1
#define GTAV_GPRLR_SECOND(a, b, ...) b
#define GTAV_GPRLR_IS_INL_(...) GTAV_GPRLR_SECOND(__VA_ARGS__, 0, ~)
#define GTAV_GPRLR_IS_INL(name) GTAV_GPRLR_IS_INL_(GTAV_GPRLR_INL_##name)
#define GTAV_GPRLR_CAT(a, b) GTAV_GPRLR_CAT_(a, b)
#define GTAV_GPRLR_CAT_(a, b) a##b
// The generated (Linux) DEFINE_REX_FUNC: <name> is a weak alias of __imp__<name>.
#define GTAV_GPRLR_DEFINE_0(name)                              \
  __attribute__((alias("__imp__" #name)))                      \
  __attribute__((weak, noinline)) extern "C" REX_FUNC(name);   \
  REX_EXTERN(__imp__##name)
// The helpers: the generated body is only __imp__<name>; <name> is the inline version below.
#define GTAV_GPRLR_DEFINE_1(name) REX_EXTERN(__imp__##name)
#undef DEFINE_REX_FUNC
#define DEFINE_REX_FUNC(name) GTAV_GPRLR_CAT(GTAV_GPRLR_DEFINE_, GTAV_GPRLR_IS_INL(name))(name)

#define GTAV_GPRLR_FUNC(name) \
  extern "C" inline __attribute__((always_inline)) void name(PPCContext& __restrict ctx, uint8_t* base)
GTAV_GPRLR_FUNC(__savegprlr_14) {
  REX_STORE_U64(ctx.r1.u32 + -152, ctx.r14.u64);
  REX_STORE_U64(ctx.r1.u32 + -144, ctx.r15.u64);
  REX_STORE_U64(ctx.r1.u32 + -136, ctx.r16.u64);
  REX_STORE_U64(ctx.r1.u32 + -128, ctx.r17.u64);
  REX_STORE_U64(ctx.r1.u32 + -120, ctx.r18.u64);
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_15) {
  REX_STORE_U64(ctx.r1.u32 + -144, ctx.r15.u64);
  REX_STORE_U64(ctx.r1.u32 + -136, ctx.r16.u64);
  REX_STORE_U64(ctx.r1.u32 + -128, ctx.r17.u64);
  REX_STORE_U64(ctx.r1.u32 + -120, ctx.r18.u64);
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_16) {
  REX_STORE_U64(ctx.r1.u32 + -136, ctx.r16.u64);
  REX_STORE_U64(ctx.r1.u32 + -128, ctx.r17.u64);
  REX_STORE_U64(ctx.r1.u32 + -120, ctx.r18.u64);
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_17) {
  REX_STORE_U64(ctx.r1.u32 + -128, ctx.r17.u64);
  REX_STORE_U64(ctx.r1.u32 + -120, ctx.r18.u64);
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_18) {
  REX_STORE_U64(ctx.r1.u32 + -120, ctx.r18.u64);
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_19) {
  REX_STORE_U64(ctx.r1.u32 + -112, ctx.r19.u64);
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_20) {
  REX_STORE_U64(ctx.r1.u32 + -104, ctx.r20.u64);
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_21) {
  REX_STORE_U64(ctx.r1.u32 + -96, ctx.r21.u64);
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_22) {
  REX_STORE_U64(ctx.r1.u32 + -88, ctx.r22.u64);
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_23) {
  REX_STORE_U64(ctx.r1.u32 + -80, ctx.r23.u64);
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_24) {
  REX_STORE_U64(ctx.r1.u32 + -72, ctx.r24.u64);
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_25) {
  REX_STORE_U64(ctx.r1.u32 + -64, ctx.r25.u64);
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_26) {
  REX_STORE_U64(ctx.r1.u32 + -56, ctx.r26.u64);
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_27) {
  REX_STORE_U64(ctx.r1.u32 + -48, ctx.r27.u64);
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_28) {
  REX_STORE_U64(ctx.r1.u32 + -40, ctx.r28.u64);
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_29) {
  REX_STORE_U64(ctx.r1.u32 + -32, ctx.r29.u64);
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_30) {
  REX_STORE_U64(ctx.r1.u32 + -24, ctx.r30.u64);
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__savegprlr_31) {
  REX_STORE_U64(ctx.r1.u32 + -16, ctx.r31.u64);
  REX_STORE_U32(ctx.r1.u32 + -8, ctx.r12.u32);
}
GTAV_GPRLR_FUNC(__restgprlr_14) {
  ctx.r14.u64 = REX_LOAD_U64(ctx.r1.u32 + -152);
  ctx.r15.u64 = REX_LOAD_U64(ctx.r1.u32 + -144);
  ctx.r16.u64 = REX_LOAD_U64(ctx.r1.u32 + -136);
  ctx.r17.u64 = REX_LOAD_U64(ctx.r1.u32 + -128);
  ctx.r18.u64 = REX_LOAD_U64(ctx.r1.u32 + -120);
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_15) {
  ctx.r15.u64 = REX_LOAD_U64(ctx.r1.u32 + -144);
  ctx.r16.u64 = REX_LOAD_U64(ctx.r1.u32 + -136);
  ctx.r17.u64 = REX_LOAD_U64(ctx.r1.u32 + -128);
  ctx.r18.u64 = REX_LOAD_U64(ctx.r1.u32 + -120);
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_16) {
  ctx.r16.u64 = REX_LOAD_U64(ctx.r1.u32 + -136);
  ctx.r17.u64 = REX_LOAD_U64(ctx.r1.u32 + -128);
  ctx.r18.u64 = REX_LOAD_U64(ctx.r1.u32 + -120);
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_17) {
  ctx.r17.u64 = REX_LOAD_U64(ctx.r1.u32 + -128);
  ctx.r18.u64 = REX_LOAD_U64(ctx.r1.u32 + -120);
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_18) {
  ctx.r18.u64 = REX_LOAD_U64(ctx.r1.u32 + -120);
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_19) {
  ctx.r19.u64 = REX_LOAD_U64(ctx.r1.u32 + -112);
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_20) {
  ctx.r20.u64 = REX_LOAD_U64(ctx.r1.u32 + -104);
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_21) {
  ctx.r21.u64 = REX_LOAD_U64(ctx.r1.u32 + -96);
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_22) {
  ctx.r22.u64 = REX_LOAD_U64(ctx.r1.u32 + -88);
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_23) {
  ctx.r23.u64 = REX_LOAD_U64(ctx.r1.u32 + -80);
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_24) {
  ctx.r24.u64 = REX_LOAD_U64(ctx.r1.u32 + -72);
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_25) {
  ctx.r25.u64 = REX_LOAD_U64(ctx.r1.u32 + -64);
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_26) {
  ctx.r26.u64 = REX_LOAD_U64(ctx.r1.u32 + -56);
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_27) {
  ctx.r27.u64 = REX_LOAD_U64(ctx.r1.u32 + -48);
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_28) {
  ctx.r28.u64 = REX_LOAD_U64(ctx.r1.u32 + -40);
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_29) {
  ctx.r29.u64 = REX_LOAD_U64(ctx.r1.u32 + -32);
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_30) {
  ctx.r30.u64 = REX_LOAD_U64(ctx.r1.u32 + -24);
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
GTAV_GPRLR_FUNC(__restgprlr_31) {
  ctx.r31.u64 = REX_LOAD_U64(ctx.r1.u32 + -16);
  ctx.r12.u64 = REX_LOAD_U32(ctx.r1.u32 + -8);
  ctx.lr = ctx.r12.u64;
}
#undef GTAV_GPRLR_FUNC
#endif  // !__APPLE__
