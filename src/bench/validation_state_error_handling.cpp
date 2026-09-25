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

// Compare the cost of different error handling styles around BlockValidationState:
//  1. return bool, with BlockValidationState as out parameter
//  2. return BlockValidationState
//  3. return util::Expected<BlockValidationState, kernel::FatalError>
//
// Each style uses a 3-level call tree: one level-1 function calling two
// level-2 functions, each calling two level-3 functions (7 functions in total).
// Level-3 functions perform a small dummy operation and return success.
// Functions are not inlined, so that the cost of passing / returning the
// state across calls is measured.
//
// cmake -B build -DBUILD_BENCH=ON
// cmake --build build -t bench_bitcoin
// build/bin/bench_bitcoin -filter='ErrorHandling.*'
//


#if defined(_MSC_VER)
#define BENCH_NOINLINE __declspec(noinline)
#else
#define BENCH_NOINLINE __attribute__((noinline))
#endif

namespace {

using ExpectedState = util::Expected<BlockValidationState, kernel::FatalError>;

constexpr uint64_t ITERATIONS{50'000'000};

// Small dummy operation done at the leaves.
inline void DummyOp(uint64_t& acc, uint64_t n) { acc = acc * 6364136223846793005ULL + n; }

// ---- Style 1: bool return, BlockValidationState out parameter ----

BENCH_NOINLINE bool BoolL3a(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 1); state = {}; return true; }
BENCH_NOINLINE bool BoolL3b(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 2); state = {}; return true; }
BENCH_NOINLINE bool BoolL3c(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 3); state = {}; return true; }
BENCH_NOINLINE bool BoolL3d(BlockValidationState& state, uint64_t& acc) { DummyOp(acc, 4); state = {}; return true; }

BENCH_NOINLINE bool BoolL2a(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolL3a(state, acc)) return false;
    if (!BoolL3b(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolL2b(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolL3c(state, acc)) return false;
    if (!BoolL3d(state, acc)) return false;
    return true;
}

BENCH_NOINLINE bool BoolL1(BlockValidationState& state, uint64_t& acc)
{
    if (!BoolL2a(state, acc)) return false;
    if (!BoolL2b(state, acc)) return false;
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

} // namespace

static void ErrorHandlingBoolInOutState(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t ok{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            BlockValidationState state;
            if (BoolL1(state, acc) && state.IsValid()) ++ok;
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

BENCHMARK(ErrorHandlingBoolInOutState);
BENCHMARK(ErrorHandlingReturnState);
BENCHMARK(ErrorHandlingReturnExpectedState);
