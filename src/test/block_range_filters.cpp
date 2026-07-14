// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilter.h>
#include <uint256.h>
#include <test/util/setup_common.h>

#include <cassert>
#include <tuple>

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(block_range_filters, BasicTestingSetup)

typedef std::vector<unsigned char> Script;

static Script RandomScript(FastRandomContext& rng) {
    size_t len = 32 + rng.randrange(100 - 32 + 1);
    return rng.randbytes(len);
}

class ScriptPool {
    std::vector<Script> scripts;
    FastRandomContext& rng;

public:
    static constexpr size_t SCRIPT_POOL_SIZE = 1'000'000;

    ScriptPool(FastRandomContext& rng, size_t size = SCRIPT_POOL_SIZE) : rng(rng) {
        scripts.resize(size);
        for (size_t i = 0; i < size; ++i) {
            scripts[i] = RandomScript(rng);
        }
    }

    size_t PickIndexWithSkewedProb() const {
        const auto index = uint64_t(
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            (double)scripts.size()
        );
        assert(index < scripts.size());
        return index;
    }

    Script PickWithSkewedProb() const {
        const auto index = PickIndexWithSkewedProb();
        assert(index < scripts.size());
        return scripts[index];
    }

    size_t size() const { return scripts.size(); }
};

class Input {
public:
    uint32_t block_height;
    uint32_t tx_index;
    uint32_t output_index;

    bool operator<(const Input& other) const
    {
        return std::tie(block_height, tx_index, output_index) <
               std::tie(other.block_height, other.tx_index, other.output_index);
    }

    void print() const {
        printf("h %d  tx %d  out %d", block_height, tx_index, output_index);
    }
};

struct Output {
    Script outscript;
};

class Blockchain;

class Transaction {
    std::vector<Input> inputs;
    std::vector<Output> outputs;

public:
    Transaction(const Blockchain& chain, std::vector<Input> inputs, std::vector<Output> outputs);

    bool HasOutIndex(uint32_t outindex) const {
        return outindex < outputs.size();
    }

    Output GetOutput(uint32_t outindex) const {
        assert(outindex < outputs.size());
        return this->outputs[outindex];
    }

    const std::vector<Input>& GetInputs() const { return this->inputs; }
    const std::vector<Output>& GetOutputs() const { return this->outputs; }
};

struct ScriptUsage {
    bool is_input;
    Script script;
    ScriptUsage(bool is_input, const Script& script) : is_input(is_input), script(std::move(script)) {}
};

class Block {
public:
    uint32_t height;
    uint256 blockhash;
    std::vector<Transaction> txs;
    //! Redundant
    std::vector<ScriptUsage> scripts;

public:
    static constexpr size_t DEFAULT_SCRIPTS_PER_BLOCK = 4500;

    //! Create empty block
    // Block() {}

    Block(const Blockchain& chain, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng);

    // static Block CreateRandom(FastRandomContext& rng) {
    //     Block block;
    //     block.SetRandomBlockHash(rng);
    //     block.scripts.reserve(DEFAULT_SCRIPTS_PER_BLOCK);
    //     for (size_t i = 0; i < DEFAULT_SCRIPTS_PER_BLOCK; ++i) {
    //         const bool is_input = rng.rand64() % 2 == 1;
    //         block.scripts.push_back({is_input, RandomScript(rng)});
    //     }
    //     return block;
    // }

    void SetRandomBlockHash(FastRandomContext& rng) {
        std::vector<unsigned char> hash(32);
        for (auto i = 0; i < 10; ++i) {
            hash[i] = 0;
        }
        for (auto i = 10; i < 32; ++i) {
            hash[i] = rng.randbytes(1)[0];
        }
        blockhash = uint256(hash);
    }

    bool HasTxindex(uint32_t txindex) const {
        return txindex < txs.size();
    }

    const Transaction& GetTxByIndex(uint32_t txindex) const {
        assert(txindex < txs.size());
        return this->txs[txindex];
    }

    Output GetOutput(uint32_t txindex, uint32_t outindex) const {
        const Transaction& tx = GetTxByIndex(txindex);
        return tx.GetOutput(outindex);
    }

    size_t GetTxCount() const { return txs.size(); }
};

class Blockchain {
    std::vector<Block> blocks;

public:
    static constexpr size_t NUM_BLOCKS = 300'000;

    //! construct empty
    Blockchain() {}

    // Blockchain(FastRandomContext& rng, size_t num_blocks = NUM_BLOCKS) {
    //     // TODO
    // }

    void AddBlock(const Block& block) {
        assert(blocks.size() == block.height);
        blocks.emplace_back(block);
    }

    bool HasBlock(uint32_t height) const {
        return height < blocks.size();
    }

    bool HasInput(const Input& input) const {
        if (!HasBlock(input.block_height)) return false;
        const Block& block = blocks[input.block_height];
        if (!block.HasTxindex(input.tx_index)) return false;
        const Transaction& tx = block.GetTxByIndex(input.tx_index);
        if (!tx.HasOutIndex(input.output_index)) return false;
        return true;
    }

    Output GetOutput(uint32_t height, uint32_t txindex, uint32_t outindex) const {
        assert(height < blocks.size());
        const Block& block = this->blocks[height];
        return block.GetOutput(txindex, outindex);
    }

    size_t size() const { return blocks.size(); }

    const Block& GetBlock(uint32_t height) const {
        return blocks[height];
    }
};

class UtxoSet {
    std::vector<Input> vector_of_utxos;
    std::map<Input, size_t> map_of_indices;
    // size_t next_index;

public:
    UtxoSet() : vector_of_utxos(), map_of_indices() {}

    void Add(const Input& input) {
        auto index = vector_of_utxos.size();
        vector_of_utxos.emplace_back(input);
        map_of_indices[input] = index;
    }

    void Remove(const Input& input) {
        auto orig_size = vector_of_utxos.size();
        // assert(map_of_indices.size() == orig_size);

        assert(map_of_indices.find(input) != map_of_indices.end());
        auto index = map_of_indices[input];
        assert(map_of_indices.erase(input) == 1);
        // assert(map_of_indices.size() == orig_size - 1);
        vector_of_utxos[index] = vector_of_utxos[vector_of_utxos.size() - 1];
        map_of_indices[vector_of_utxos[index]] = index;
        vector_of_utxos.erase(vector_of_utxos.begin() + (vector_of_utxos.size() - 1));

        assert(vector_of_utxos.size() == orig_size - 1);
        // assert(map_of_indices.size() == orig_size - 1);
    }

    Input PickAndRemoveRandom(FastRandomContext& rng) {
        const auto index = size_t(
            (double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max() * (double)vector_of_utxos.size()
        );
        assert(index < vector_of_utxos.size());
        Input input = vector_of_utxos[index];
        // printf("Picked index %ld of %ld, input: ", index, vector_of_utxos.size()); input.print(); printf("\n");
        Remove(input);
        return input;
    }

    size_t size() const { return vector_of_utxos.size(); }
};

class BlockChainManager {
    ScriptPool scriptpool;
    Blockchain chain;
    UtxoSet utxos;
    FastRandomContext& rng;

public:
    BlockChainManager(FastRandomContext& rng, size_t scriptpool_size = ScriptPool::SCRIPT_POOL_SIZE) :
        scriptpool(rng, scriptpool_size), chain(), utxos(),
        rng(rng)
    {
        AddBlock(CreateGenesisBlock());
    }
 
    void AddBlock(Block block)
    {
        // printf("Adding block %d (utxos: %ld)...\n", block.height, utxos.size());
        chain.AddBlock(block);
        // update UTXOS
        for (uint32_t txindex = 0; txindex < block.txs.size(); ++txindex) {
            const auto& tx = block.txs[txindex];
            // // remove spent -- Done in GenerateTxsForNextBlock
            // for (const auto& i : tx.GetInputs()) {
            //     utxos.Remove(i);
            // }
            // add new unspent
            for (uint32_t outindex = 0; outindex < tx.GetOutputs().size(); ++outindex) {
                utxos.Add(Input{block.height, txindex, outindex});
            }
        }
        printf("Added block %d with %ld txs, utxos: %ld\n", block.height, block.txs.size(), utxos.size());
    }

    Transaction CreateGenesisTx() const {
        auto script1 = scriptpool.PickWithSkewedProb();
        return Transaction(chain, {}, {Output{script1}});
    }

    Block CreateGenesisBlock() const {
        auto tx1 = CreateGenesisTx();
        auto genesis_block = Block(chain, 0, {tx1}, rng);
        return genesis_block;
    }

    std::vector<Transaction> GenerateTxsForNextBlock(size_t desired_tx_num)
    {
        auto num = std::max(std::min(utxos.size() / 3, desired_tx_num), size_t(1));
        std::vector<Transaction> txs;
        txs.reserve(num);
        txs.emplace_back(CreateGenesisTx());
        for (size_t i = 0; i < num; ++i) {
            if (utxos.size() == 0) break;
            // TODO more flexible input and output count
            std::vector<Input> inputs;
            auto numinputs = std::min(size_t(2), utxos.size());
            for (size_t i = 0; i < numinputs; ++i) {
                Input inp = utxos.PickAndRemoveRandom(rng);
                inputs.emplace_back(inp);
            }
            std::vector<Output> outputs;
            auto numoutputs = 2;
            for (auto i = 0; i < numoutputs; ++i) {
                outputs.emplace_back(Output{scriptpool.PickWithSkewedProb()});
            }
            auto tx = Transaction(chain, inputs, outputs);
            txs.emplace_back(tx);
        }
        return txs;
    }

    void AddNewBlock(size_t desired_tx_num) {
        auto txs = GenerateTxsForNextBlock(desired_tx_num);
        auto height = chain.size();
        auto block = Block(chain, height, txs, rng);
        AddBlock(block);
    }

    size_t size() const { return chain.size(); }

    const Block& GetBlock(uint32_t height) const { return chain.GetBlock(height); }
};

Transaction::Transaction(const Blockchain& chain, std::vector<Input> inputs, std::vector<Output> outputs) {
    this->inputs.clear();
    for (const auto& i : inputs) {
        if (!chain.HasInput(i)) {
            printf("Tx::Tx HasInput failed! "); i.print(); printf(" \n");
        }
        assert(chain.HasInput(i));
        this->inputs.emplace_back(i);
    }
    this->outputs.clear();
    for (const auto& o : outputs) {
        this->outputs.emplace_back(o);
    }
}

Block::Block(const Blockchain& chain, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng) {
    this->height = height;
    SetRandomBlockHash(rng);

    txs = transactions;

    this->scripts.clear();
    this->scripts.reserve(DEFAULT_SCRIPTS_PER_BLOCK);
    for (const auto& t : this->txs) {
        for (const auto& i : t.GetInputs()) {
            const auto script = chain.GetOutput(i.block_height, i.tx_index, i.output_index);
            this->scripts.emplace_back(true, script.outscript);
        }
        for (const auto& o : t.GetOutputs()) {
            this->scripts.emplace_back(false, o.outscript);
        }
    }
}

static GCSFilter BuildFilterForBlock(const Block& block)
{
    GCSFilter::ElementSet elements;
    for (const auto& script : block.scripts) {
        elements.insert(script.script);
    }
    GCSFilter::Params params;
    params.m_siphash_k0 = block.blockhash.GetUint64(0);
    params.m_siphash_k1 = block.blockhash.GetUint64(1);
    params.m_P = BASIC_FILTER_P;
    params.m_M = BASIC_FILTER_M;
    return GCSFilter(params, elements);
}

class BlockRangeFilterBuilder {
    GCSFilter::ElementSet elements;
    uint256 blockhash;
public:
    BlockRangeFilterBuilder() : blockhash(uint256::ZERO) {}
    void AddBlock(const Block& block) {
        if (blockhash == uint256::ZERO) {
            blockhash = block.blockhash;
        }
        for (const auto& script : block.scripts) {
            elements.insert(script.script);
        }
    }
    GCSFilter Finish() {
        GCSFilter::Params params;
        params.m_siphash_k0 = blockhash.GetUint64(0);
        params.m_siphash_k1 = blockhash.GetUint64(1);
        params.m_P = BASIC_FILTER_P;
        params.m_M = BASIC_FILTER_M;
        return GCSFilter(params, elements);
    }
};

// BOOST_AUTO_TEST_CASE(filter_for_one_block)
// {
//     const auto block1 = Block::CreateRandom(m_rng);

//     GCSFilter filter = BuildFilterForBlock(block1);

//     for (const auto& script : block1.scripts) {
//         BOOST_CHECK(filter.Match(script.script));
//     }
// }

BOOST_AUTO_TEST_CASE(whole_blockchain)
{
    auto manager = BlockChainManager(m_rng, ScriptPool::SCRIPT_POOL_SIZE);

    printf("Creating blocks...\n");
    //for (auto i = 0; i < 10'000; ++i) {
    for (auto i = 0; i < 4'000; ++i) {
        manager.AddNewBlock(3'500); // TODO into const
    }

    // for (size_t i = 0; i < manager.size(); ++i) {
    //     printf("%ld: txs: %ld\n", i, manager.GetBlock(i).GetTxCount());
    // }

    {
        printf("Creating block filters...\n");
        std::vector<GCSFilter> filters(manager.size());
        uint64_t total_filter_size = 0;
        for (size_t i = 0; i < manager.size(); ++i) {
            const auto filter = BuildFilterForBlock(manager.GetBlock(i));
            auto filter_size = filter.GetEncoded().size();
            total_filter_size += filter_size;
            printf("%ld: filter size: %ld\n", i, filter_size);
            filters.push_back(filter);
        }
        printf("Total filter size: %ld\n", total_filter_size);
    }

    {
        const uint32_t block_range_size = 10;
        printf("Creating block-range filters, range size %d ...\n", block_range_size);
        uint64_t total_range_filter_size = 0;
        for (size_t i = 0; i < manager.size(); i += block_range_size) {
            BlockRangeFilterBuilder builder;
            for (size_t j = 0; j < block_range_size && i + j < manager.size(); ++j) {
                builder.AddBlock(manager.GetBlock(i + j));
            }
            const auto filter = builder.Finish();
            auto filter_size = filter.GetEncoded().size();
            total_range_filter_size += filter_size;
            printf("%ld: filter size: %ld\n", i, filter_size);
            //filters.push_back(filter);
        }
        printf("Total range filter size: %ld\n", total_range_filter_size);
    }

    // printf("scriptpool %ld\n", scriptpool.size());
    // for (auto i = 0; i < 20; ++i) {
    //     printf("  %ld\n", scriptpool.PickIndexWithSkewedProb(m_rng));
    // }
}

BOOST_AUTO_TEST_SUITE_END()
