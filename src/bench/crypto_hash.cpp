// Copyright (c) 2016-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#include <bench/bench.h>
#include <crypto/muhash.h>
#include <crypto/ripemd160.h>
#include <crypto/sha1.h>
#include <crypto/sha256.h>
#include <crypto/sha3.h>
#include <crypto/sha512.h>
#include <crypto/siphash.h>
#include <random.h>
#include <span.h>
#include <tinyformat.h>
#include <uint256.h>
#include <util/hasher.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <vector>

/* Number of bytes to hash per iteration */
static const uint64_t BUFFER_SIZE = 1000*1000;

static void BenchRIPEMD160(benchmark::Bench& bench)
{
    uint8_t hash[CRIPEMD160::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CRIPEMD160().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void SHA1(benchmark::Bench& bench)
{
    uint8_t hash[CSHA1::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA1().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void SHA256_STANDARD(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::STANDARD)));
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    });
    SHA256AutoDetect();
}

static void SHA256_SSE4(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4)));
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    });
    SHA256AutoDetect();
}

static void SHA256_AVX2(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_AVX2)));
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    });
    SHA256AutoDetect();
}

static void SHA256_SHANI(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_SHANI)));
    uint8_t hash[CSHA256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256().Write(in.data(), in.size()).Finalize(hash);
    });
    SHA256AutoDetect();
}

static void SHA3_256_1M(benchmark::Bench& bench)
{
    uint8_t hash[SHA3_256::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        SHA3_256().Write(in).Finalize(hash);
    });
}

static void SHA256_32b_STANDARD(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::STANDARD)));
    std::vector<uint8_t> in(32,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256()
            .Write(in.data(), in.size())
            .Finalize(in.data());
    });
    SHA256AutoDetect();
}

static void SHA256_32b_SSE4(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4)));
    std::vector<uint8_t> in(32,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256()
            .Write(in.data(), in.size())
            .Finalize(in.data());
    });
    SHA256AutoDetect();
}

static void SHA256_32b_AVX2(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_AVX2)));
    std::vector<uint8_t> in(32,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256()
            .Write(in.data(), in.size())
            .Finalize(in.data());
    });
    SHA256AutoDetect();
}

static void SHA256_32b_SHANI(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_SHANI)));
    std::vector<uint8_t> in(32,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA256()
            .Write(in.data(), in.size())
            .Finalize(in.data());
    });
    SHA256AutoDetect();
}

static void SHA256D64_1024_STANDARD(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::STANDARD)));
    std::vector<uint8_t> in(64 * 1024, 0);
    bench.batch(in.size()).unit("byte").run([&] {
        SHA256D64(in.data(), in.data(), 1024);
    });
    SHA256AutoDetect();
}

static void SHA256D64_1024_SSE4(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4)));
    std::vector<uint8_t> in(64 * 1024, 0);
    bench.batch(in.size()).unit("byte").run([&] {
        SHA256D64(in.data(), in.data(), 1024);
    });
    SHA256AutoDetect();
}

static void SHA256D64_1024_AVX2(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_AVX2)));
    std::vector<uint8_t> in(64 * 1024, 0);
    bench.batch(in.size()).unit("byte").run([&] {
        SHA256D64(in.data(), in.data(), 1024);
    });
    SHA256AutoDetect();
}

static void SHA256D64_1024_SHANI(benchmark::Bench& bench)
{
    bench.name(strprintf("%s using the '%s' SHA256 implementation", __func__, SHA256AutoDetect(sha256_implementation::USE_SSE4_AND_SHANI)));
    std::vector<uint8_t> in(64 * 1024, 0);
    bench.batch(in.size()).unit("byte").run([&] {
        SHA256D64(in.data(), in.data(), 1024);
    });
    SHA256AutoDetect();
}

static void SHA512(benchmark::Bench& bench)
{
    uint8_t hash[CSHA512::OUTPUT_SIZE];
    std::vector<uint8_t> in(BUFFER_SIZE,0);
    bench.batch(in.size()).unit("byte").run([&] {
        CSHA512().Write(in.data(), in.size()).Finalize(hash);
    });
}

static void SipHash24_32b(benchmark::Bench& bench)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedSipHasher presalted_sip_hasher(rng.rand64(), rng.rand64());
    auto val{rng.rand256()};
    auto i{0U};
    bench.run([&] {
        ankerl::nanobench::doNotOptimizeAway(presalted_sip_hasher(val));
        ++i;
        val.data()[i % uint256::size()] ^= i & 0xFF;
    });
}

static void SipHash24_36b(benchmark::Bench& bench)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedSipHasher presalted_sip_hasher(rng.rand64(), rng.rand64());
    auto val{rng.rand256()};
    uint32_t extra{rng.rand32()};
    auto i{0U};
    bench.run([&] {
        ankerl::nanobench::doNotOptimizeAway(presalted_sip_hasher(val, extra));
        ++i;
        val.data()[i % uint256::size()] ^= i & 0xFF;
        extra += i;
    });
}

static void SipHash13Jumbo_36b(benchmark::Bench& bench)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedSipHasher13Jumbo presalted_sip_hasher(rng.rand64(), rng.rand64());
    auto val{rng.rand256()};
    uint32_t extra{rng.rand32()};
    auto i{0U};
    bench.run([&] {
        ankerl::nanobench::doNotOptimizeAway(presalted_sip_hasher(val, extra));
        ++i;
        val.data()[i % uint256::size()] ^= i & 0xFF;
        extra += i;
    });
}

static void XorHash_36b(benchmark::Bench& bench)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedXorHasher presalted_xor_hasher(rng.rand64());
    auto val{rng.rand256()};
    uint32_t extra{rng.rand32()};
    auto i{0U};
    bench.run([&] {
        ankerl::nanobench::doNotOptimizeAway(presalted_xor_hasher(val, extra));
        ++i;
        val.data()[i % uint256::size()] ^= i & 0xFF;
        extra += i;
    });
}

/*
 * Avalanche / bit-independence diagnostics for hashers of the form
 * uint64_t operator()(const uint256& val, uint32_t extra).
 *
 * These are not timing benchmarks: they run once (no bench.run() call, see
 * BenchRunner::RunAll) and print a statistical report instead of a timing
 * row. They're registered as benchmarks anyway so they're filterable and
 * discoverable via the usual `bench_bitcoin -filter=` / `-list` machinery.
 */
namespace {

constexpr int AVALANCHE_INPUT_BITS = 256 + 32;
constexpr int AVALANCHE_OUTPUT_BITS = 64;
constexpr int AVALANCHE_TRIALS = 10000;

struct AvalancheStats {
    //! Average fraction of output bits that flip when a single input bit is flipped (ideal: 0.5).
    double mean_flip_fraction;
    //! Mean, over the full input-bit x output-bit matrix, of |P(output bit flips) - 0.5|.
    double bic_mean_abs_dev;
    //! Worst single (input bit, output bit) deviation from 0.5 found in the matrix.
    double bic_max_abs_dev;
};

//! Flip bit `bit` (0..287) of the logical 288-bit (val || extra) input.
void FlipInputBit(uint256& val, uint32_t& extra, int bit)
{
    if (bit < 256) {
        val.data()[bit / 8] ^= (1 << (bit % 8));
    } else {
        extra ^= (1u << (bit - 256));
    }
}

template <typename HashFn>
AvalancheStats ComputeAvalancheStats(FastRandomContext& rng, int trials, HashFn&& hash_fn)
{
    std::vector<std::array<int, AVALANCHE_OUTPUT_BITS>> flip_counts(AVALANCHE_INPUT_BITS);
    for (auto& row : flip_counts) row.fill(0);
    int64_t total_flips{0};

    for (int t = 0; t < trials; ++t) {
        const uint256 val{rng.rand256()};
        const uint32_t extra{rng.rand32()};
        const uint64_t base{hash_fn(val, extra)};

        for (int bit = 0; bit < AVALANCHE_INPUT_BITS; ++bit) {
            uint256 flipped_val{val};
            uint32_t flipped_extra{extra};
            FlipInputBit(flipped_val, flipped_extra, bit);

            const uint64_t diff{base ^ hash_fn(flipped_val, flipped_extra)};
            total_flips += std::popcount(diff);
            for (int ob = 0; ob < AVALANCHE_OUTPUT_BITS; ++ob) {
                if (diff & (uint64_t{1} << ob)) ++flip_counts[bit][ob];
            }
        }
    }

    AvalancheStats stats;
    stats.mean_flip_fraction = double(total_flips) / (double(trials) * AVALANCHE_INPUT_BITS * AVALANCHE_OUTPUT_BITS);

    double max_dev{0.0};
    double sum_dev{0.0};
    for (int bit = 0; bit < AVALANCHE_INPUT_BITS; ++bit) {
        for (int ob = 0; ob < AVALANCHE_OUTPUT_BITS; ++ob) {
            const double p{double(flip_counts[bit][ob]) / trials};
            const double dev{std::abs(p - 0.5)};
            max_dev = std::max(max_dev, dev);
            sum_dev += dev;
        }
    }
    stats.bic_mean_abs_dev = sum_dev / (AVALANCHE_INPUT_BITS * AVALANCHE_OUTPUT_BITS);
    stats.bic_max_abs_dev = max_dev;
    return stats;
}

void PrintAvalancheReport(const std::string& name, const AvalancheStats& stats)
{
    tfm::format(std::cout,
                "Avalanche report for %s (%d trials, %d input bits, %d output bits):\n"
                "  mean output-bit flip fraction per input-bit flip: %.4f (ideal 0.5)\n"
                "  bit-independence mean |deviation from 0.5|:       %.4f\n"
                "  bit-independence max  |deviation from 0.5|:       %.4f\n",
                name, AVALANCHE_TRIALS, AVALANCHE_INPUT_BITS, AVALANCHE_OUTPUT_BITS,
                stats.mean_flip_fraction, stats.bic_mean_abs_dev, stats.bic_max_abs_dev);
}

} // namespace

static void Avalanche_SipHash(benchmark::Bench&)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedSipHasher hasher(rng.rand64(), rng.rand64());
    const auto stats{ComputeAvalancheStats(rng, AVALANCHE_TRIALS, [&](const uint256& v, uint32_t e) { return hasher(v, e); })};
    PrintAvalancheReport("PresaltedSipHasher", stats);
}

static void Avalanche_JumboHash(benchmark::Bench&)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedSipHasher13Jumbo hasher(rng.rand64(), rng.rand64());
    const auto stats{ComputeAvalancheStats(rng, AVALANCHE_TRIALS, [&](const uint256& v, uint32_t e) { return hasher(v, e); })};
    PrintAvalancheReport("PresaltedSipHasher13Jumbo", stats);
}

static void Avalanche_XorHash(benchmark::Bench&)
{
    FastRandomContext rng{/*fDeterministic=*/true};
    PresaltedXorHasher hasher(rng.rand64());
    const auto stats{ComputeAvalancheStats(rng, AVALANCHE_TRIALS, [&](const uint256& v, uint32_t e) { return hasher(v, e); })};
    PrintAvalancheReport("PresaltedXorHasher", stats);
}

static void MuHash(benchmark::Bench& bench)
{
    MuHash3072 acc;
    unsigned char key[32] = {0};
    uint32_t i = 0;
    bench.run([&] {
        key[0] = ++i & 0xFF;
        acc *= MuHash3072(key);
    });
}

static void MuHashMul(benchmark::Bench& bench)
{
    MuHash3072 acc;
    FastRandomContext rng(true);
    MuHash3072 muhash{rng.randbytes(32)};

    bench.run([&] {
        acc *= muhash;
    });
}

static void MuHashDiv(benchmark::Bench& bench)
{
    MuHash3072 acc;
    FastRandomContext rng(true);
    MuHash3072 muhash{rng.randbytes(32)};

    bench.run([&] {
        acc /= muhash;
    });
}

static void MuHashPrecompute(benchmark::Bench& bench)
{
    MuHash3072 acc;
    FastRandomContext rng(true);
    std::vector<unsigned char> key{rng.randbytes(32)};

    bench.run([&] {
        MuHash3072{key};
    });
}

static void MuHashFinalize(benchmark::Bench& bench)
{
    FastRandomContext rng(true);
    MuHash3072 acc{rng.randbytes(32)};
    acc /= MuHash3072{rng.rand256()};

    bench.run([&] {
        uint256 out;
        acc.Finalize(out);
        acc /= MuHash3072{out};
    });
}

BENCHMARK(BenchRIPEMD160);
BENCHMARK(SHA1);
BENCHMARK(SHA256_STANDARD);
BENCHMARK(SHA256_SSE4);
BENCHMARK(SHA256_AVX2);
BENCHMARK(SHA256_SHANI);
BENCHMARK(SHA512);
BENCHMARK(SHA3_256_1M);

BENCHMARK(SHA256_32b_STANDARD);
BENCHMARK(SHA256_32b_SSE4);
BENCHMARK(SHA256_32b_AVX2);
BENCHMARK(SHA256_32b_SHANI);
BENCHMARK(SipHash24_32b);
BENCHMARK(SipHash24_36b);
BENCHMARK(SipHash13Jumbo_36b);
BENCHMARK(XorHash_36b);
BENCHMARK(Avalanche_SipHash);
BENCHMARK(Avalanche_JumboHash);
BENCHMARK(Avalanche_XorHash);
BENCHMARK(SHA256D64_1024_STANDARD);
BENCHMARK(SHA256D64_1024_SSE4);
BENCHMARK(SHA256D64_1024_AVX2);
BENCHMARK(SHA256D64_1024_SHANI);

BENCHMARK(MuHash);
BENCHMARK(MuHashMul);
BENCHMARK(MuHashDiv);
BENCHMARK(MuHashPrecompute);
BENCHMARK(MuHashFinalize);
