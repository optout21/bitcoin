// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <consensus/validation.h>
#include <kernel/notifications_interface.h>
#include <util/expected.h>

#include <cassert>
#include <cstdint>
#include <utility>
#include <variant>

// Compare the cost of different error handling styles around BlockValidationState:
//  0. baseline: return void, no state (measures call overhead + dummy work only)
//  1. return bool, with BlockValidationState as parameter
//     a. out-only: leaves set the state on success too (`state = {}`)
//     b. in-out: leaves do not touch the state on success (as in the codebase)
//  2. return BlockValidationState
//  3. return util::Expected<BlockValidationState, kernel::FatalError>
//  4. return util::Expected<void, std::variant<BlockValidationState, kernel::FatalError>>
//     (nothing on success; the invalid state or the fatal error on failure)
//
// Each style uses a 3-level call tree: one level-1 function calling two
// level-2 functions, each calling two level-3 functions (7 functions in total).
// Level-3 functions perform a small dummy operation and return success,
// except for every FAIL_RATE-th level-3 call, which fails with an invalid
// state (in the way of the given style). Failures are propagated to the top
// immediately, skipping the remaining calls, so each failing level-3 call
// makes exactly one top-level call fail.
// Functions are not inlined, so that the cost of passing / returning the
// state across calls is measured. They are also shielded from other
// interprocedural optimizations (GCC: noipa; Clang: external linkage prevents
// dead argument elimination), which would otherwise e.g. drop the unused
// state parameter of style 1b, making it identical to the baseline.
//
// cmake -B build -DBUILD_BENCH=ON
// cmake --build build -t bench_bitcoin
// build/bin/bench_bitcoin -filter='ErrorHandling.*'
//


#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#elif defined(__has_attribute)
#if __has_attribute(noipa)
#define BENCH_NOINLINE __attribute__((noipa))
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

// Named (not anonymous) namespace: external linkage, see above.
namespace error_handling_bench {

using ExpectedState = util::Expected<BlockValidationState, kernel::FatalError>;
using BlockValidationFailure = std::variant<BlockValidationState, kernel::FatalError>;
using VoidExpected = util::Expected<void, BlockValidationFailure>;

constexpr uint64_t ITERATIONS{50'000'000};
// Every FAIL_RATE-th level-3 call fails.
constexpr uint64_t FAIL_RATE{1000};

// Small dummy operation done at the leaves. Counts the calls in `counter`, and
// returns false (failure) for every FAIL_RATE-th call.
[[nodiscard]] inline bool DummyOp(uint64_t& acc, uint64_t& counter, uint64_t n)
{
    acc = acc * 6364136223846793005ULL + n;
    return ++counter % FAIL_RATE != 0;
}

// Mark the state invalid, as a failing leaf does.
inline bool Fail(BlockValidationState& state)
{
    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-dummy-failure");
}

inline BlockValidationState InvalidState()
{
    BlockValidationState state;
    Fail(state);
    return state;
}

// ---- Style 0: baseline, void return, no state ----

BENCH_NOINLINE void VoidL3a(uint64_t& acc, uint64_t& counter) { (void)DummyOp(acc, counter, 1); }
BENCH_NOINLINE void VoidL3b(uint64_t& acc, uint64_t& counter) { (void)DummyOp(acc, counter, 2); }
BENCH_NOINLINE void VoidL3c(uint64_t& acc, uint64_t& counter) { (void)DummyOp(acc, counter, 3); }
BENCH_NOINLINE void VoidL3d(uint64_t& acc, uint64_t& counter) { (void)DummyOp(acc, counter, 4); }

BENCH_NOINLINE void VoidL2a(uint64_t& acc, uint64_t& counter)
{
    VoidL3a(acc, counter);
    VoidL3b(acc, counter);
}

BENCH_NOINLINE void VoidL2b(uint64_t& acc, uint64_t& counter)
{
    VoidL3c(acc, counter);
    VoidL3d(acc, counter);
}

BENCH_NOINLINE void VoidL1(uint64_t& acc, uint64_t& counter)
{
    VoidL2a(acc, counter);
    VoidL2b(acc, counter);
}

// ---- Style 1a: bool return, BlockValidationState out-only parameter (leaves set it on success) ----

BENCH_NOINLINE bool BoolOutL3a(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 1)) return Fail(state); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3b(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 2)) return Fail(state); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3c(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 3)) return Fail(state); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3d(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 4)) return Fail(state); state = {}; return true; }

BENCH_NOINLINE bool BoolOutL2a(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolOutL3a(state, acc, counter)) return false;
    if (!BoolOutL3b(state, acc, counter)) return false;
    return true;
}

BENCH_NOINLINE bool BoolOutL2b(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolOutL3c(state, acc, counter)) return false;
    if (!BoolOutL3d(state, acc, counter)) return false;
    return true;
}

BENCH_NOINLINE bool BoolOutL1(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolOutL2a(state, acc, counter)) return false;
    if (!BoolOutL2b(state, acc, counter)) return false;
    return true;
}

// ---- Style 1b: bool return, BlockValidationState in-out parameter (untouched on success) ----

BENCH_NOINLINE bool BoolInOutL3a(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 1)) return Fail(state); return true; }
BENCH_NOINLINE bool BoolInOutL3b(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 2)) return Fail(state); return true; }
BENCH_NOINLINE bool BoolInOutL3c(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 3)) return Fail(state); return true; }
BENCH_NOINLINE bool BoolInOutL3d(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 4)) return Fail(state); return true; }

BENCH_NOINLINE bool BoolInOutL2a(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolInOutL3a(state, acc, counter)) return false;
    if (!BoolInOutL3b(state, acc, counter)) return false;
    return true;
}

BENCH_NOINLINE bool BoolInOutL2b(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolInOutL3c(state, acc, counter)) return false;
    if (!BoolInOutL3d(state, acc, counter)) return false;
    return true;
}

BENCH_NOINLINE bool BoolInOutL1(BlockValidationState& state, uint64_t& acc, uint64_t& counter)
{
    if (!BoolInOutL2a(state, acc, counter)) return false;
    if (!BoolInOutL2b(state, acc, counter)) return false;
    return true;
}

// ---- Style 2: return BlockValidationState ----

BENCH_NOINLINE BlockValidationState StateL3a(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 1)) return InvalidState(); return {}; }
BENCH_NOINLINE BlockValidationState StateL3b(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 2)) return InvalidState(); return {}; }
BENCH_NOINLINE BlockValidationState StateL3c(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 3)) return InvalidState(); return {}; }
BENCH_NOINLINE BlockValidationState StateL3d(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 4)) return InvalidState(); return {}; }

BENCH_NOINLINE BlockValidationState StateL2a(uint64_t& acc, uint64_t& counter)
{
    if (auto state{StateL3a(acc, counter)}; !state.IsValid()) return state;
    if (auto state{StateL3b(acc, counter)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState StateL2b(uint64_t& acc, uint64_t& counter)
{
    if (auto state{StateL3c(acc, counter)}; !state.IsValid()) return state;
    if (auto state{StateL3d(acc, counter)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState StateL1(uint64_t& acc, uint64_t& counter)
{
    if (auto state{StateL2a(acc, counter)}; !state.IsValid()) return state;
    if (auto state{StateL2b(acc, counter)}; !state.IsValid()) return state;
    return {};
}

// ---- Style 3: return util::Expected<BlockValidationState, kernel::FatalError> ----

BENCH_NOINLINE ExpectedState ExpL3a(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 1)) return InvalidState(); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3b(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 2)) return InvalidState(); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3c(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 3)) return InvalidState(); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3d(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 4)) return InvalidState(); return BlockValidationState{}; }

BENCH_NOINLINE ExpectedState ExpL2a(uint64_t& acc, uint64_t& counter)
{
    if (auto res{ExpL3a(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL3b(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

BENCH_NOINLINE ExpectedState ExpL2b(uint64_t& acc, uint64_t& counter)
{
    if (auto res{ExpL3c(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL3d(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

BENCH_NOINLINE ExpectedState ExpL1(uint64_t& acc, uint64_t& counter)
{
    if (auto res{ExpL2a(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL2b(acc, counter)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

// ---- Style 4: return util::Expected<void, std::variant<BlockValidationState, kernel::FatalError>> ----

BENCH_NOINLINE VoidExpected VoidExpL3a(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 1)) return util::Unexpected{InvalidState()}; return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3b(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 2)) return util::Unexpected{InvalidState()}; return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3c(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 3)) return util::Unexpected{InvalidState()}; return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3d(uint64_t& acc, uint64_t& counter) { if (!DummyOp(acc, counter, 4)) return util::Unexpected{InvalidState()}; return {}; }

BENCH_NOINLINE VoidExpected VoidExpL2a(uint64_t& acc, uint64_t& counter)
{
    if (auto res{VoidExpL3a(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL3b(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

BENCH_NOINLINE VoidExpected VoidExpL2b(uint64_t& acc, uint64_t& counter)
{
    if (auto res{VoidExpL3c(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL3d(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

BENCH_NOINLINE VoidExpected VoidExpL1(uint64_t& acc, uint64_t& counter)
{
    if (auto res{VoidExpL2a(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL2b(acc, counter)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

// Each failing level-3 call makes exactly one top-level call fail.
void CheckCounts(uint64_t ok, uint64_t failed, uint64_t counter)
{
    assert(failed > 0);
    assert((ok + failed) % ITERATIONS == 0);
    assert(failed == counter / FAIL_RATE);
}

} // namespace error_handling_bench

using namespace error_handling_bench;

static void ErrorHandlingBaselineVoid(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            VoidL1(acc, counter);
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
}

static void ErrorHandlingBoolOutState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t failed{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            BlockValidationState state;
            if (BoolOutL1(state, acc, counter) && state.IsValid()) {
                ++ok;
            } else {
                ++failed;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, failed, counter);
}

static void ErrorHandlingBoolInOutState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t failed{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            BlockValidationState state;
            if (BoolInOutL1(state, acc, counter) && state.IsValid()) {
                ++ok;
            } else {
                ++failed;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, failed, counter);
}

static void ErrorHandlingReturnState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t failed{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (StateL1(acc, counter).IsValid()) {
                ++ok;
            } else {
                ++failed;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, failed, counter);
}

static void ErrorHandlingReturnExpectedState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t failed{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (auto res{ExpL1(acc, counter)}; res && res->IsValid()) {
                ++ok;
            } else {
                ++failed;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, failed, counter);
}

static void ErrorHandlingReturnVoidExpected(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t failed{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (VoidExpL1(acc, counter)) {
                ++ok;
            } else {
                ++failed;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, failed, counter);
}

BENCHMARK(ErrorHandlingBaselineVoid);
BENCHMARK(ErrorHandlingBoolOutState);
BENCHMARK(ErrorHandlingBoolInOutState);
BENCHMARK(ErrorHandlingReturnState);
BENCHMARK(ErrorHandlingReturnExpectedState);
BENCHMARK(ErrorHandlingReturnVoidExpected);
