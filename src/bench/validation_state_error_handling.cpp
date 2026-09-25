// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bench/bench.h>
#include <consensus/validation.h>
#include <kernel/notifications_interface.h>
#include <util/expected.h>
#include <util/translation.h>

#include <cassert>
#include <cstdint>
#include <numeric>
#include <stdexcept>
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
//  5. return BlockValidationState (valid or invalid), throw on fatal error
//
// Each style uses a 3-level call tree: one level-1 function calling two
// level-2 functions, each calling two level-3 functions (7 functions in total).
// Level-3 functions perform a small dummy operation, which has one of three
// outcomes: valid (most calls), invalid (every INVALID_RATE-th level-3 call),
// or fatal error (every ERROR_RATE-th level-3 call). Each style reports the
// outcome in its own way. Styles 1 and 2 cannot represent a fatal error, as
// BlockValidationState no longer has an error mode; they report it as invalid.
// Failures are propagated to the top immediately, skipping the remaining
// calls, so each failing level-3 call makes exactly one top-level call fail.
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
// Every INVALID_RATE-th level-3 call is invalid.
constexpr uint64_t INVALID_RATE{1000};
// Every ERROR_RATE-th level-3 call has a fatal error. Offset by half a period,
// so that invalid and error calls never coincide.
constexpr uint64_t ERROR_RATE{1000};
constexpr uint64_t ERROR_OFFSET{ERROR_RATE / 2};
static_assert(ERROR_RATE >= 2);
static_assert(ERROR_OFFSET % std::gcd(INVALID_RATE, ERROR_RATE) != 0, "invalid and error calls must never coincide");

enum class Outcome { VALID, INVALID, FATAL_ERROR };

// Small dummy operation done at the leaves. Counts the calls in `counter`, and
// decides the outcome from it.
[[nodiscard]] inline Outcome DummyOp(uint64_t& acc, uint64_t& counter, uint64_t n)
{
    acc = acc * 6364136223846793005ULL + n;
    ++counter;
    if (counter % INVALID_RATE == 0) return Outcome::INVALID;
    if (counter % ERROR_RATE == ERROR_OFFSET) return Outcome::FATAL_ERROR;
    return Outcome::VALID;
}

inline bool SetInvalid(BlockValidationState& state)
{
    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "bad-dummy-invalid");
}

// BlockValidationState cannot represent a fatal error: styles 1 and 2 report it as invalid.
inline bool SetErrorAsInvalid(BlockValidationState& state)
{
    return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS, "dummy-fatal-error");
}

inline BlockValidationState InvalidState()
{
    BlockValidationState state;
    SetInvalid(state);
    return state;
}

inline BlockValidationState ErrorAsInvalidState()
{
    BlockValidationState state;
    SetErrorAsInvalid(state);
    return state;
}

// Receives the fatalError notifications; the default implementation does nothing.
kernel::Notifications g_notifications;

inline util::Unexpected<kernel::FatalError> RaiseError()
{
    return kernel::FatalError::Raise(g_notifications, Untranslated("dummy fatal error"));
}

// Leaf results for each style, from the outcome of DummyOp().

inline bool BoolOutLeaf(BlockValidationState& state, Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: state = {}; return true;
    case Outcome::INVALID: return SetInvalid(state);
    case Outcome::FATAL_ERROR: return SetErrorAsInvalid(state);
    }
    assert(false);
}

inline bool BoolInOutLeaf(BlockValidationState& state, Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: return true;
    case Outcome::INVALID: return SetInvalid(state);
    case Outcome::FATAL_ERROR: return SetErrorAsInvalid(state);
    }
    assert(false);
}

inline BlockValidationState StateLeaf(Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: return {};
    case Outcome::INVALID: return InvalidState();
    case Outcome::FATAL_ERROR: return ErrorAsInvalidState();
    }
    assert(false);
}

inline ExpectedState ExpLeaf(Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: return BlockValidationState{};
    case Outcome::INVALID: return InvalidState();
    case Outcome::FATAL_ERROR: return RaiseError();
    }
    assert(false);
}

// Thrown on fatal error by style 5. kernel::FatalError itself cannot be
// thrown, as it is not copyable.
class FatalErrorException : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

inline BlockValidationState ThrowLeaf(Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: return {};
    case Outcome::INVALID: return InvalidState();
    case Outcome::FATAL_ERROR: throw FatalErrorException{RaiseError().error().message()};
    }
    assert(false);
}

inline VoidExpected VoidExpLeaf(Outcome outcome)
{
    switch (outcome) {
    case Outcome::VALID: return {};
    case Outcome::INVALID: return util::Unexpected{InvalidState()};
    case Outcome::FATAL_ERROR: return RaiseError();
    }
    assert(false);
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

BENCH_NOINLINE bool BoolOutL3a(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolOutLeaf(state, DummyOp(acc, counter, 1)); }
BENCH_NOINLINE bool BoolOutL3b(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolOutLeaf(state, DummyOp(acc, counter, 2)); }
BENCH_NOINLINE bool BoolOutL3c(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolOutLeaf(state, DummyOp(acc, counter, 3)); }
BENCH_NOINLINE bool BoolOutL3d(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolOutLeaf(state, DummyOp(acc, counter, 4)); }

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

BENCH_NOINLINE bool BoolInOutL3a(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolInOutLeaf(state, DummyOp(acc, counter, 1)); }
BENCH_NOINLINE bool BoolInOutL3b(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolInOutLeaf(state, DummyOp(acc, counter, 2)); }
BENCH_NOINLINE bool BoolInOutL3c(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolInOutLeaf(state, DummyOp(acc, counter, 3)); }
BENCH_NOINLINE bool BoolInOutL3d(BlockValidationState& state, uint64_t& acc, uint64_t& counter) { return BoolInOutLeaf(state, DummyOp(acc, counter, 4)); }

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

BENCH_NOINLINE BlockValidationState StateL3a(uint64_t& acc, uint64_t& counter) { return StateLeaf(DummyOp(acc, counter, 1)); }
BENCH_NOINLINE BlockValidationState StateL3b(uint64_t& acc, uint64_t& counter) { return StateLeaf(DummyOp(acc, counter, 2)); }
BENCH_NOINLINE BlockValidationState StateL3c(uint64_t& acc, uint64_t& counter) { return StateLeaf(DummyOp(acc, counter, 3)); }
BENCH_NOINLINE BlockValidationState StateL3d(uint64_t& acc, uint64_t& counter) { return StateLeaf(DummyOp(acc, counter, 4)); }

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

BENCH_NOINLINE ExpectedState ExpL3a(uint64_t& acc, uint64_t& counter) { return ExpLeaf(DummyOp(acc, counter, 1)); }
BENCH_NOINLINE ExpectedState ExpL3b(uint64_t& acc, uint64_t& counter) { return ExpLeaf(DummyOp(acc, counter, 2)); }
BENCH_NOINLINE ExpectedState ExpL3c(uint64_t& acc, uint64_t& counter) { return ExpLeaf(DummyOp(acc, counter, 3)); }
BENCH_NOINLINE ExpectedState ExpL3d(uint64_t& acc, uint64_t& counter) { return ExpLeaf(DummyOp(acc, counter, 4)); }

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

BENCH_NOINLINE VoidExpected VoidExpL3a(uint64_t& acc, uint64_t& counter) { return VoidExpLeaf(DummyOp(acc, counter, 1)); }
BENCH_NOINLINE VoidExpected VoidExpL3b(uint64_t& acc, uint64_t& counter) { return VoidExpLeaf(DummyOp(acc, counter, 2)); }
BENCH_NOINLINE VoidExpected VoidExpL3c(uint64_t& acc, uint64_t& counter) { return VoidExpLeaf(DummyOp(acc, counter, 3)); }
BENCH_NOINLINE VoidExpected VoidExpL3d(uint64_t& acc, uint64_t& counter) { return VoidExpLeaf(DummyOp(acc, counter, 4)); }

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

// ---- Style 5: return BlockValidationState, throw on fatal error ----

BENCH_NOINLINE BlockValidationState ThrowL3a(uint64_t& acc, uint64_t& counter) { return ThrowLeaf(DummyOp(acc, counter, 1)); }
BENCH_NOINLINE BlockValidationState ThrowL3b(uint64_t& acc, uint64_t& counter) { return ThrowLeaf(DummyOp(acc, counter, 2)); }
BENCH_NOINLINE BlockValidationState ThrowL3c(uint64_t& acc, uint64_t& counter) { return ThrowLeaf(DummyOp(acc, counter, 3)); }
BENCH_NOINLINE BlockValidationState ThrowL3d(uint64_t& acc, uint64_t& counter) { return ThrowLeaf(DummyOp(acc, counter, 4)); }

BENCH_NOINLINE BlockValidationState ThrowL2a(uint64_t& acc, uint64_t& counter)
{
    if (auto state{ThrowL3a(acc, counter)}; !state.IsValid()) return state;
    if (auto state{ThrowL3b(acc, counter)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState ThrowL2b(uint64_t& acc, uint64_t& counter)
{
    if (auto state{ThrowL3c(acc, counter)}; !state.IsValid()) return state;
    if (auto state{ThrowL3d(acc, counter)}; !state.IsValid()) return state;
    return {};
}

BENCH_NOINLINE BlockValidationState ThrowL1(uint64_t& acc, uint64_t& counter)
{
    if (auto state{ThrowL2a(acc, counter)}; !state.IsValid()) return state;
    if (auto state{ThrowL2b(acc, counter)}; !state.IsValid()) return state;
    return {};
}

// Each failing level-3 call makes exactly one top-level call fail, so the
// failure counts follow from the number of level-3 calls.
uint64_t ExpectedInvalidCount(uint64_t counter) { return counter / INVALID_RATE; }
uint64_t ExpectedErrorCount(uint64_t counter) { return (counter + ERROR_RATE - ERROR_OFFSET) / ERROR_RATE; }

// For styles that report fatal errors as invalid (styles 1 and 2).
void CheckCounts(uint64_t ok, uint64_t failed, uint64_t counter)
{
    assert(failed > 0);
    assert((ok + failed) % ITERATIONS == 0);
    assert(failed == ExpectedInvalidCount(counter) + ExpectedErrorCount(counter));
}

// For styles that distinguish invalid and fatal error (styles 3, 4 and 5).
void CheckCounts(uint64_t ok, uint64_t invalid, uint64_t error, uint64_t counter)
{
    assert(invalid > 0 && error > 0);
    assert((ok + invalid + error) % ITERATIONS == 0);
    assert(invalid == ExpectedInvalidCount(counter));
    assert(error == ExpectedErrorCount(counter));
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
    uint64_t invalid{0};
    uint64_t error{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (auto res{ExpL1(acc, counter)}; !res) {
                ++error;
            } else if (res->IsValid()) {
                ++ok;
            } else {
                ++invalid;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, invalid, error, counter);
}

static void ErrorHandlingReturnVoidExpected(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t invalid{0};
    uint64_t error{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            if (auto res{VoidExpL1(acc, counter)}; res) {
                ++ok;
            } else if (std::holds_alternative<BlockValidationState>(res.error())) {
                ++invalid;
            } else {
                ++error;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, invalid, error, counter);
}

static void ErrorHandlingReturnStateThrow(benchmark::Bench& bench)
{
    uint64_t acc{0};
    uint64_t counter{0};
    uint64_t ok{0};
    uint64_t invalid{0};
    uint64_t error{0};
    bench.batch(ITERATIONS).unit("call").run([&] {
        for (uint64_t i{0}; i < ITERATIONS; ++i) {
            try {
                if (ThrowL1(acc, counter).IsValid()) {
                    ++ok;
                } else {
                    ++invalid;
                }
            } catch (const FatalErrorException&) {
                ++error;
            }
        }
        ankerl::nanobench::doNotOptimizeAway(acc);
    });
    CheckCounts(ok, invalid, error, counter);
}

BENCHMARK(ErrorHandlingBaselineVoid);
BENCHMARK(ErrorHandlingBoolOutState);
BENCHMARK(ErrorHandlingBoolInOutState);
BENCHMARK(ErrorHandlingReturnState);
BENCHMARK(ErrorHandlingReturnExpectedState);
BENCHMARK(ErrorHandlingReturnVoidExpected);
BENCHMARK(ErrorHandlingReturnStateThrow);
