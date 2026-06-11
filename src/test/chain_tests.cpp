// Copyright (c) The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <boost/test/unit_test.hpp>

#include <blockfilter.h>
#include <chain.h>
#include <test/util/setup_common.h>

#include <array>
#include <memory>
#include <random>
#include <unordered_set>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(chain_tests, BasicTestingSetup)

constexpr int id_len{80};

class Block {
public:
    std::unordered_set<GCSFilter::Element, ByteVectorHash> elems;

    Block() : elems({}) {}

    //! Create a random ID
    static GCSFilter::Element CreateRandomElem() {
        std::mt19937 rng(std::random_device{}());
        std::vector<unsigned char> id(id_len);
        for (auto i{0}; i < id_len; ++i) {
            id[i] = static_cast<uint8_t>(rng() & 0xFF);
        };
        return id;
    }

    //! Create a block with n random elements
    static Block FillWithRandom(size_t n) {
        Block b;
        for (size_t i{0}; i < n; ++i) {
            b.elems.insert(CreateRandomElem());
        }
        return b;
    }
};

BOOST_AUTO_TEST_CASE(multi_range_filter_benchmark)
{
    constexpr int block_range_size{2016};
    std::vector<Block> bb(block_range_size);
    for (auto i{0}; i < block_range_size; ++i) {
        bb[i] = Block::FillWithRandom(3500 + i/10);
        //printf("Generated a block\n");
    }
    printf("Generated %d blocks\n", block_range_size);

    GCSFilter::Params params;
    params.m_siphash_k0 = 0x0001020304050607;
    params.m_siphash_k1 = 0x08090a0b0c0d0e0f;
    params.m_P = BASIC_FILTER_P;
    params.m_M = BASIC_FILTER_M;

    uint64_t tot_size{0};
    for (auto i{0}; i < block_range_size; ++i) {
        auto filter = GCSFilter(params, bb[i].elems);
        auto filter_size = filter.GetEncoded().size();
        printf("filter size %ld\n", filter_size);
        tot_size += filter_size;
    }
    printf("Total filter size %ld (1 filter for each of the %d blocks)\n", tot_size, block_range_size);

    // append all elems into one big
    std::unordered_set<GCSFilter::Element, ByteVectorHash> range;
    for (auto i{0}; i < block_range_size; ++i) {
        for (auto& b : bb[i].elems) {
            range.insert(b);
        }
    }
    // create filter for the range
    auto range_filter = GCSFilter(params, range);
    printf("Range filter size %ld\n", range_filter.GetEncoded().size());
}

namespace {

const CBlockIndex* NaiveGetAncestor(const CBlockIndex* a, int height)
{
    while (a->nHeight > height) {
        a = a->pprev;
    }
    BOOST_REQUIRE_EQUAL(a->nHeight, height);
    return a;
}

const CBlockIndex* NaiveLastCommonAncestor(const CBlockIndex* a, const CBlockIndex* b)
{
    while (a->nHeight > b->nHeight) {
        a = a->pprev;
    }
    while (b->nHeight > a->nHeight) {
        b = b->pprev;
    }
    while (a != b) {
        BOOST_REQUIRE_EQUAL(a->nHeight, b->nHeight);
        a = a->pprev;
        b = b->pprev;
    }
    BOOST_REQUIRE_EQUAL(a, b);
    return a;
}

} // namespace

BOOST_AUTO_TEST_CASE(basic_tests)
{
    // An empty chain
    const CChain chain_0;
    // A chain with 2 blocks
    CChain chain_2;

    CBlockIndex genesis;
    genesis.nHeight = 0;
    chain_2.SetTip(genesis);

    CBlockIndex bi1;
    bi1.pprev = &genesis;
    bi1.nHeight = 1;
    chain_2.SetTip(bi1);

    BOOST_CHECK_EQUAL(chain_0.Height(), -1);
    BOOST_CHECK_EQUAL(chain_2.Height(), 1);

    BOOST_CHECK_EQUAL(chain_0.Tip(), nullptr);
    BOOST_CHECK_EQUAL(chain_2.Tip(), &bi1);

    // Indexer accessor: call with valid and invalid (low & high) values
    BOOST_CHECK_EQUAL(chain_2[-1], nullptr);
    BOOST_CHECK_EQUAL(chain_2[0], &genesis);
    BOOST_CHECK_EQUAL(chain_2[1], &bi1);
    BOOST_CHECK_EQUAL(chain_2[2], nullptr);

    // Contains: call with contained & non-contained blocks
    BOOST_CHECK(chain_2.Contains(genesis));
    BOOST_CHECK(chain_2.Contains(bi1));
    BOOST_CHECK(!chain_0.Contains(genesis));

    // Call with non-tip & tip blocks
    BOOST_CHECK_EQUAL(chain_2.Next(genesis), &bi1);
    BOOST_CHECK_EQUAL(chain_2.Next(bi1), nullptr);
    BOOST_CHECK_EQUAL(chain_0.Next(genesis), nullptr);

    BOOST_CHECK_EQUAL(chain_2.Genesis(), &genesis);
    BOOST_CHECK_EQUAL(chain_0.Genesis(), nullptr);
}

BOOST_AUTO_TEST_CASE(findfork_tests)
{
    // Create a forking chain
    const auto init_branch{[](auto& blocks, CBlockIndex* parent, int start_height) {
        for (size_t i{0}; i < blocks.size(); ++i) {
            blocks[i].pprev = i == 0 ? parent : &blocks[i - 1];
            blocks[i].nHeight = start_height + i;
        }
    }};

    const auto check_same{[](const CChain& chain, const auto& blocks) {
        for (const auto& block : blocks) {
            BOOST_CHECK_EQUAL(chain.FindFork(block), &block);
        }
    }};

    const auto check_fork_point{[](const CChain& chain, const auto& blocks, const CBlockIndex& fork_point) {
        for (const auto& block : blocks) {
            BOOST_CHECK_EQUAL(chain.FindFork(block), &fork_point);
        }
    }};

    std::array<CBlockIndex, 10> blocks_common;
    init_branch(blocks_common, nullptr, 0);

    std::array<CBlockIndex, 10> blocks_long;
    init_branch(blocks_long, &blocks_common.back(), blocks_common.size());

    std::array<CBlockIndex, 5> blocks_short;
    init_branch(blocks_short, &blocks_common.back(), blocks_common.size());

    // Create a chain with the longer fork
    CChain chain_long;
    chain_long.SetTip(blocks_long.back());
    BOOST_CHECK_EQUAL(chain_long.Height(), 10 + 10 - 1);
    // Test the blocks in the common part -> result should be the same
    check_same(chain_long, blocks_common);
    // Test the blocks on the longer fork -> result should be the same
    check_same(chain_long, blocks_long);
    // Test the blocks on the other shorter fork -> result should be the fork point
    check_fork_point(chain_long, blocks_short, blocks_common.back());

    // Create a chain with the shorter fork
    CChain chain_short;
    chain_short.SetTip(blocks_short.back());
    BOOST_CHECK_EQUAL(chain_short.Height(), 10 + 5 - 1);
    // Test the blocks in the common part -> result should be the same
    check_same(chain_short, blocks_common);
    // Test the blocks on the shorter fork -> result should be the same
    check_same(chain_short, blocks_short);
    // Test the blocks on the other longer fork -> result should be the fork point
    check_fork_point(chain_short, blocks_long, blocks_common.back());

    // Invalid test case. Mixing chains is not supported
    CBlockIndex block_on_unrelated_chain;
    BOOST_CHECK_EQUAL(chain_long.FindFork(block_on_unrelated_chain), nullptr);
}

BOOST_AUTO_TEST_CASE(chain_test)
{
    FastRandomContext ctx;
    std::vector<std::unique_ptr<CBlockIndex>> block_index;
    // Run 10 iterations of the whole test.
    for (int i = 0; i < 10; ++i) {
        block_index.clear();
        // Create genesis block.
        auto genesis = std::make_unique<CBlockIndex>();
        genesis->nHeight = 0;
        block_index.push_back(std::move(genesis));
        // Create 10000 more blocks.
        for (int b = 0; b < 10000; ++b) {
            auto new_index = std::make_unique<CBlockIndex>();
            // 95% of blocks build on top of the last block; the others fork off randomly.
            if (ctx.randrange(20) != 0) {
                new_index->pprev = block_index.back().get();
            } else {
                new_index->pprev = block_index[ctx.randrange(block_index.size())].get();
            }
            new_index->nHeight = new_index->pprev->nHeight + 1;
            new_index->BuildSkip();
            block_index.push_back(std::move(new_index));
        }
        // Run 10000 random GetAncestor queries.
        for (int q = 0; q < 10000; ++q) {
            const CBlockIndex* block = block_index[ctx.randrange(block_index.size())].get();
            unsigned height = ctx.randrange<unsigned>(block->nHeight + 1);
            const CBlockIndex* result = block->GetAncestor(height);
            BOOST_CHECK(result == NaiveGetAncestor(block, height));
        }
        // Run 10000 random LastCommonAncestor queries.
        for (int q = 0; q < 10000; ++q) {
            const CBlockIndex* block1 = block_index[ctx.randrange(block_index.size())].get();
            const CBlockIndex* block2 = block_index[ctx.randrange(block_index.size())].get();
            const CBlockIndex* result = LastCommonAncestor(block1, block2);
            BOOST_CHECK(result == NaiveLastCommonAncestor(block1, block2));
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
