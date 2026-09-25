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
// Level-3 functions perform a small dummy operation and return success.
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

// Small dummy operation done at the leaves.
inline void DummyOp(uint64_t& acc, uint64_t n) { acc = acc * 6364136223846793005ULL + n; }

// ---- Style 0: baseline, void return, no state ----

BENCH_NOINLINE void VoidL3a(uint64_t& acc) { DummyOp(acc, 1); }
BENCH_NOINLINE void VoidL3b(uint64_t& acc) { DummyOp(acc, 2); }
BENCH_NOINLINE void VoidL3c(uint64_t& acc) { DummyOp(acc, 3); }
BENCH_NOINLINE void VoidL3d(uint64_t& acc) { DummyOp(acc, 4); }

BENCH_NOINLINE void VoidL2a(uint64_t& acc)
{
    VoidL3a(acc);
    VoidL3b(acc);
}

BENCH_NOINLINE void VoidL2b(uint64_t& acc)
{
    VoidL3c(acc);
    VoidL3d(acc);
}

BENCH_NOINLINE void VoidL1(uint64_t& acc)
{
    VoidL2a(acc);
    VoidL2b(acc);
}

// ---- Style 1a: bool return, BlockValidationState out-only parameter (leaves set it on success) ----

BENCH_NOINLINE bool BoolOutL3a(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 1); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3b(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 2); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3c(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 3); state = {}; return true; }
BENCH_NOINLINE bool BoolOutL3d(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 4); state = {}; return true; }

BENCH_NOINLINE bool BoolOutL2a(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolOutL3a(state, acc)) return false;
    if (!BoolOutL3b(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolOutL2b(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolOutL3c(state, acc)) return false;
    if (!BoolOutL3d(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolOutL1(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolOutL2a(state, acc)) return false;
    if (!BoolOutL2b(state, acc)) return false;
    return true;
}

// ---- Style 1b: bool return, BlockValidationState in-out parameter (untouched on success) ----

BENCH_NOINLINE bool BoolInOutL3a(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 1); return true; }
BENCH_NOINLINE bool BoolInOutL3b(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 2); return true; }
BENCH_NOINLINE bool BoolInOutL3c(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 3); return true; }
BENCH_NOINLINE bool BoolInOutL3d(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 4); return true; }

BENCH_NOINLINE bool BoolInOutL2a(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolInOutL3a(state, acc)) return false;
    if (!BoolInOutL3b(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolInOutL2b(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolInOutL3c(state, acc)) return false;
    if (!BoolInOutL3d(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolInOutL1(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolInOutL2a(state, acc)) return false;
    if (!BoolInOutL2b(state, acc)) return false;
    return true;
}

// ---- Style 2: return BlockValidationState ----

BENCH_NOINLINE BlockValidationState StateL3a(uint64_t& acc) { DummyOp(acc, 1); return {}; }
BENCH_NOINLINE BlockValidationState StateL3b(uint64_t& acc) { DummyOp(acc, 2); return {}; }
BENCH_NOINLINE BlockValidationState StateL3c(uint64_t& acc) { DummyOp(acc, 3); return {}; }
BENCH_NOINLINE BlockValidationState StateL3d(uint64_t& acc) { DummyOp(acc, 4); return {}; }

BENCH_NOINLINE BlockValidationState StateL2a(uint64_t& acc)
{
    if (auto state{StateL3a(acc)}; !state.IsValid()) return state;
    if (auto state{StateL3b(acc)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState StateL2b(uint64_t& acc)
{
    if (auto state{StateL3c(acc)}; !state.IsValid()) return state;
    if (auto state{StateL3d(acc)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState StateL1(uint64_t& acc)
{
    if (auto state{StateL2a(acc)}; !state.IsValid()) return state;
    if (auto state{StateL2b(acc)}; !state.IsValid()) return state;
    return {};
}

// ---- Style 3: return util::Expected<BlockValidationState, kernel::FatalError> ----

BENCH_NOINLINE ExpectedState ExpL3a(uint64_t& acc) { DummyOp(acc, 1); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3b(uint64_t& acc) { DummyOp(acc, 2); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3c(uint64_t& acc) { DummyOp(acc, 3); return BlockValidationState{}; }
BENCH_NOINLINE ExpectedState ExpL3d(uint64_t& acc) { DummyOp(acc, 4); return BlockValidationState{}; }

BENCH_NOINLINE ExpectedState ExpL2a(uint64_t& acc)
{
    if (auto res{ExpL3a(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL3b(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

BENCH_NOINLINE ExpectedState ExpL2b(uint64_t& acc)
{
    if (auto res{ExpL3c(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL3d(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

BENCH_NOINLINE ExpectedState ExpL1(uint64_t& acc)
{
    if (auto res{ExpL2a(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    if (auto res{ExpL2b(acc)}; !res) {
        return util::Unexpected{std::move(res.error())};
    } else if (!res->IsValid()) {
        return res;
    }
    return BlockValidationState{};
}

// ---- Style 4: return util::Expected<void, std::variant<BlockValidationState, kernel::FatalError>> ----

BENCH_NOINLINE VoidExpected VoidExpL3a(uint64_t& acc) { DummyOp(acc, 1); return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3b(uint64_t& acc) { DummyOp(acc, 2); return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3c(uint64_t& acc) { DummyOp(acc, 3); return {}; }
BENCH_NOINLINE VoidExpected VoidExpL3d(uint64_t& acc) { DummyOp(acc, 4); return {}; }

BENCH_NOINLINE VoidExpected VoidExpL2a(uint64_t& acc)
{
    if (auto res{VoidExpL3a(acc)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL3b(acc)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

BENCH_NOINLINE VoidExpected VoidExpL2b(uint64_t& acc)
{
    if (auto res{VoidExpL3c(acc)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL3d(acc)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

BENCH_NOINLINE VoidExpected VoidExpL1(uint64_t& acc)
{
    if (auto res{VoidExpL2a(acc)}; !res) return util::Unexpected{std::move(res.error())};
    if (auto res{VoidExpL2b(acc)}; !res) return util::Unexpected{std::move(res.error())};
    return {};
}

} // namespace error_handling_bench

using namespace error_handling_bench;

static void ErrorHandlingBaselineVoid(benchmark::Bench& bench)
{
    uint64_t acc{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            VoidL1(acc);
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
}

static void ErrorHandlingBoolOutState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            BlockValidationState state;
            if (BoolOutL1(state, acc) && state.IsValid()) ++ok;
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    assert(ok % ITERATIONS == 0);
}

static void ErrorHandlingBoolInOutState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            BlockValidationState state;
            if (BoolInOutL1(state, acc) && state.IsValid()) ++ok;
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    assert(ok % ITERATIONS == 0);
}

static void ErrorHandlingReturnState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (StateL1(acc).IsValid()) ++ok;
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    assert(ok % ITERATIONS == 0);
}

static void ErrorHandlingReturnExpectedState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (auto res{ExpL1(acc)}; res && res->IsValid()) ++ok;
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    assert(ok % ITERATIONS == 0);
}

static void ErrorHandlingReturnVoidExpected(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (VoidExpL1(acc)) ++ok;
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    assert(ok % ITERATIONS == 0);
}

BENCHMARK(ErrorHandlingBaselineVoid);
BENCHMARK(ErrorHandlingBoolOutState);
BENCHMARK(ErrorHandlingBoolInOutState);
BENCHMARK(ErrorHandlingReturnState);
BENCHMARK(ErrorHandlingReturnExpectedState);
BENCHMARK(ErrorHandlingReturnVoidExpected);
