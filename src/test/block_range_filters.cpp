// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <blockfilter.h>
#include <uint256.h>
#include <test/util/setup_common.h>

#include <cassert>
#include <tuple>

#include <boost/test/unit_test.hpp>

// const uint64_t BLOCK_COUNT = 20'000;
const uint64_t BLOCK_COUNT = 20'000;
const uint64_t SKIP_BLOCKS = 0;
const uint64_t TX_PER_BLOCK = 3'000;
const uint8_t HEADER_SIZE = 22;
const uint8_t INPUT_SIZE = 32 + 4 + 4;

BOOST_FIXTURE_TEST_SUITE(block_range_filters, BasicTestingSetup)

typedef std::vector<unsigned char> Script;

static Script RandomScript(FastRandomContext& rng) {
    size_t len = 32 + rng.randrange(100 - 32 + 1);
    return rng.randbytes(len);
}

typedef size_t ScriptIdx;

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

    const Script& GetScript(size_t index) const {
        assert(index < this->scripts.size());
        return this->scripts[index];
    }

    ScriptIdx PickIndexWithSkewedProb() const {
        const auto index = uint64_t(
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            ((double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max()) *
            (double)scripts.size()
        );
        assert(index < scripts.size());
        return index;
    }

    //! Pick a script with a skewed probability, return script
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

class Output {
    //! script index
    ScriptIdx script_idx;

public:
    Output(ScriptIdx script_idx) : script_idx(script_idx) {}

    ScriptIdx ScIdx() const { return script_idx; }
};

typedef size_t InputIdx;

class InputPool {
    std::vector<Input> inputs;
public:
    InputPool() : inputs() {}

    InputIdx Add(Input input) {
        InputIdx idx = inputs.size();
        inputs.emplace_back(input);
        return idx;
    }

    const Input& GetInput(InputIdx idx) const {
        assert(idx < this->inputs.size());
        return this->inputs[idx];
    }
};

class Blockchain;

class Transaction {
    std::vector<InputIdx> inputs;
    std::vector<Output> outputs;
    uint64_t rough_size;

public:
    Transaction(const Blockchain& chain, const ScriptPool& scriptpool, const InputPool& inputpool, std::vector<InputIdx> inputs, std::vector<Output> outputs);

    bool HasOutIndex(uint16_t outindex) const {
        return outindex < outputs.size();
    }

    // Check if transaction matches a script (input or output)
    // bool MatchesScript(const Script& script) const;

    uint64_t GetRoughSize() const { return rough_size; }

    Output GetOutput(uint16_t outindex) const {
        assert(outindex < outputs.size());
        return this->outputs[outindex];
    }

    const std::vector<InputIdx>& GetInputs() const { return this->inputs; }
    const std::vector<Output>& GetOutputs() const { return this->outputs; }
};

struct ScriptUsage {
    uint16_t txindex;
    bool is_input;
    ScriptIdx script_idx;
    ScriptUsage(uint16_t txindex, bool is_input, ScriptIdx script_idx) :
        txindex(txindex), is_input(is_input), script_idx(script_idx) {}
};

class Block {
public:
    uint32_t height;
    uint256 blockhash;
    std::vector<Transaction> txs;
    //! All scripts in the block txs (input & output). Redundant
    std::vector<ScriptUsage> scripts;
    //! Total rough size of the block
    uint64_t total_rough_size;

public:
    static constexpr size_t DEFAULT_SCRIPTS_PER_BLOCK = 4500;

    //! Create empty block
    // Block() {}

    Block(const Blockchain& chain, const InputPool& inputpool, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng);

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

    bool HasTxindex(uint16_t txindex) const {
        return txindex < txs.size();
    }

    const Transaction& GetTxByIndex(uint16_t txindex) const {
        assert(txindex < txs.size());
        return this->txs[txindex];
    }

    Output GetOutput(uint16_t txindex, uint16_t outindex) const {
        const Transaction& tx = GetTxByIndex(txindex);
        return tx.GetOutput(outindex);
    }

    // Return all transactions matching a script (input or output)
    std::vector<Transaction> GetTxsByScript(ScriptIdx script_idx) const {
        std::vector<Transaction> res;
        for (const auto& sc: scripts) {
            if (sc.script_idx == script_idx) {
                res.emplace_back(GetTxByIndex(sc.txindex));
            }
        }
        return res;
    }

    size_t GetTxCount() const { return txs.size(); }

    uint64_t GetRoughSize() const { return total_rough_size; }
};

class Blockchain {
    std::vector<Block> blocks;

public:
    static constexpr size_t NUM_BLOCKS = 300'000;

    //! construct empty
    Blockchain() {}

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

    Output GetOutput(uint32_t height, uint16_t txindex, uint16_t outindex) const {
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
    std::vector<InputIdx> vector_of_utxos;
    std::map<InputIdx, size_t> map_of_indices;
    // size_t next_index;

public:
    UtxoSet() : vector_of_utxos(), map_of_indices() {}

    void Add(InputIdx input_idx) {
        auto index = vector_of_utxos.size();
        vector_of_utxos.emplace_back(input_idx);
        map_of_indices[input_idx] = index;
    }

    void Remove(InputIdx input_idx) {
        auto orig_size = vector_of_utxos.size();
        // assert(map_of_indices.size() == orig_size);

        assert(map_of_indices.find(input_idx) != map_of_indices.end());
        auto index = map_of_indices[input_idx];
        assert(map_of_indices.erase(input_idx) == 1);
        // assert(map_of_indices.size() == orig_size - 1);
        vector_of_utxos[index] = vector_of_utxos[vector_of_utxos.size() - 1];
        map_of_indices[vector_of_utxos[index]] = index;
        vector_of_utxos.erase(vector_of_utxos.begin() + (vector_of_utxos.size() - 1));

        assert(vector_of_utxos.size() == orig_size - 1);
        // assert(map_of_indices.size() == orig_size - 1);
    }

    InputIdx PickAndRemoveRandom(FastRandomContext& rng) {
        const auto index = size_t(
            (double)rng.rand64() / (double)std::numeric_limits<uint64_t>::max() * (double)vector_of_utxos.size()
        );
        assert(index < vector_of_utxos.size());
        InputIdx input_idx = vector_of_utxos[index];
        // printf("Picked index %ld of %ld, input: ", index, vector_of_utxos.size()); input.print(); printf("\n");
        Remove(input_idx);
        return input_idx;
    }

    size_t size() const { return vector_of_utxos.size(); }
};

// Index of script occurances (input or output) to blocks where they are present
class ScriptIndex {
    //! Map from script index to block heights (where it appears)
    std::map<uint64_t, std::unordered_set<uint32_t>> index;

public:
    ScriptIndex() : index() {}

    void Add(ScriptIdx script_idx, uint32_t block_height) {
        if (index.find(script_idx) == index.end()) {
            index[script_idx] = std::unordered_set<uint32_t>{};
        }
        index[script_idx].insert(block_height);
    }

    bool Contains(ScriptIdx script_idx) const {
        return index.find(script_idx) != index.end();
    }

    size_t CountBlocks(ScriptIdx script_idx) const {
        auto it = index.find(script_idx);
        if (it == index.end()) {
            return 0;
        }
        return it->second.size();
    }

    std::unordered_set<uint32_t> GetBlocks(ScriptIdx script_idx) const {
        auto it = index.find(script_idx);
        if (it == index.end()) {
            return std::unordered_set<uint32_t>{};
        }
        return it->second;
    }

    size_t size() const { return index.size(); }
};

static GCSFilter BuildFilterForBlock(const Block& block, const ScriptPool& scriptpool)
{
    GCSFilter::ElementSet elements;
    for (const auto& script : block.scripts) {
        elements.insert(scriptpool.GetScript(script.script_idx));
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
    const ScriptPool& scriptpool;
public:
    BlockRangeFilterBuilder(const ScriptPool& scriptpool) : blockhash(uint256::ZERO), scriptpool(scriptpool) {}
    void AddBlock(const Block& block) {
        if (blockhash == uint256::ZERO) {
            blockhash = block.blockhash;
        }
        for (const auto& script : block.scripts) {
            elements.insert(this->scriptpool.GetScript(script.script_idx));
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

struct SimulationResult {
    uint16_t block_range_size;
    uint64_t total_filter_size;
    uint64_t total_dl;
};

class BlockChainManager {
    ScriptPool scriptpool;
    InputPool inputpool;
    Blockchain chain;
    UtxoSet utxo_set;
    ScriptIndex script_index;
    // Store block filters here (one per block)
    std::vector<GCSFilter> block_filters;
    // Total size for all block filters
    uint64_t total_block_filter_size;
    // Store block range filter here, index is block range size (4, 16, ...)
    std::map<uint16_t, std::vector<GCSFilter>> block_range_filter_sets;
    FastRandomContext& rng;

public:
    BlockChainManager(FastRandomContext& rng, size_t scriptpool_size = ScriptPool::SCRIPT_POOL_SIZE) :
        scriptpool(rng, scriptpool_size), inputpool(), chain(), utxo_set(), script_index(),
        rng(rng)
    {
        AddBlock(CreateGenesisBlock());
    }
 
    void AddBlock(const Block& block)
    {
        // printf("Adding block %d (utxos: %ld)...\n", block.height, utxo_set.size());
        chain.AddBlock(block);
        // Update UTXOS and ScriptIndex
        for (uint16_t txindex = 0; txindex < block.txs.size(); ++txindex) {
            const auto& tx = block.txs[txindex];
            for (const auto& inp_idx : tx.GetInputs()) {
                // Utxos remove spent -- Done in GenerateTxsForNextBlock
                // utxo_set.Remove(i);
                const auto& inp = inputpool.GetInput(inp_idx);
                const auto& spent = chain.GetOutput(inp.block_height, inp.tx_index, inp.output_index);
                script_index.Add(spent.ScIdx(), block.height);
            }
            // ScriptIndex: inputs: done in GenerateTxsForNextBlock
            for (uint16_t outindex = 0; outindex < tx.GetOutputs().size(); ++outindex) {
                // Utxos: add new unspent
                Input input{block.height, txindex, outindex};
                InputIdx inp_idx = inputpool.Add(input);
                utxo_set.Add(inp_idx);
                // ScriptIndex: add outputs
                script_index.Add(tx.GetOutput(outindex).ScIdx(), block.height);
            }
        }
        if (block.height % 50 == 0) {
            printf("Added block %d with %ld txs, size %ld ; utxos: %ld, scripts: %ld \n",
                block.height, block.txs.size(), block.GetRoughSize(), utxo_set.size(), script_index.size());
        }
    }

    Transaction CreateGenesisTx() const {
        auto script_index1 = scriptpool.PickIndexWithSkewedProb();
        return Transaction(chain, scriptpool, inputpool, {}, {Output(script_index1)});
    }

    Block CreateGenesisBlock() const {
        auto tx1 = CreateGenesisTx();
        auto genesis_block = Block(chain, inputpool, 0, {tx1}, rng);
        return genesis_block;
    }

    std::vector<Transaction> GenerateTxsForNextBlock(size_t desired_tx_num, uint32_t block_height)
    {
        auto num = std::max(std::min(utxo_set.size() / 3, desired_tx_num), size_t(1));
        std::vector<Transaction> txs;
        txs.reserve(num);
        txs.emplace_back(CreateGenesisTx());
        for (size_t i = 0; i < num; ++i) {
            if (utxo_set.size() == 0) break;
            // TODO more flexible input and output count
            std::vector<InputIdx> inputs;
            auto numinputs = std::min(size_t(2), utxo_set.size());
            for (size_t i = 0; i < numinputs; ++i) {
                InputIdx inp_idx = utxo_set.PickAndRemoveRandom(rng);
                // auto inp_idx = this->inputpool.Add(inp);
                inputs.emplace_back(inp_idx);
            }
            std::vector<Output> outputs;
            auto numoutputs = 2;
            for (auto i = 0; i < numoutputs; ++i) {
                const auto output_script_index = scriptpool.PickIndexWithSkewedProb();
                script_index.Add(output_script_index, block_height);
                outputs.emplace_back(Output(output_script_index));
            }
            auto tx = Transaction(chain, scriptpool, inputpool, inputs, outputs);
            txs.emplace_back(tx);
        }
        return txs;
    }

    void AddNewBlock(size_t desired_tx_num) {
        auto height = chain.size();
        auto txs = GenerateTxsForNextBlock(desired_tx_num, height);
        auto block = Block(chain, inputpool, height, txs, rng);
        AddBlock(block);
    }

    size_t size() const { return chain.size(); }

    const Block& GetBlock(uint32_t height) const { return chain.GetBlock(height); }

    uint64_t BlockFiltersCount() const { return block_filters.size(); }

    uint64_t BlockFiltersTotalSize() const { return total_block_filter_size; }

    void AnalyzeScriptFrequencies() const {
        const int n = 1000;
        printf("Analyzing script occurence counts %d ... \n", n);
        for (auto i = 0; i < n; ++i) {
            const auto script_idx = scriptpool.PickIndexWithSkewedProb();
            const auto count = script_index.CountBlocks(script_idx);
            printf("  c %ld \n", count);
        }
    }

    uint64_t PickScriptIndexWithDesiredBlockOccurance(size_t min, size_t max) const {
        size_t tries = 0;
        while (tries < 1'000'000) {
            const auto script_idx = scriptpool.PickIndexWithSkewedProb();
            const auto count = script_index.CountBlocks(script_idx);
            if (count >= min && count <= max) {
                printf("Picked a script with %ld block occurance (%ld -- %ld, %ld tries) \n", count, min, max, tries+1);
                return script_idx;
            }
            ++tries;
        }
        assert(false); // not found in time
        return scriptpool.PickIndexWithSkewedProb(); // fallback
    }

    void CreateBlockFilters() {
        total_block_filter_size = 0; // total size for all block filters
        printf("Creating block filters... (size: %ld) \n", size());
        block_filters.reserve(size());
        for (size_t i = SKIP_BLOCKS; i < size(); ++i) {
            const auto filter = BuildFilterForBlock(GetBlock(i), this->scriptpool);
            auto filter_size = filter.GetEncoded().size();
            total_block_filter_size += filter_size;
            // printf("%ld: filter size: %ld\n", i, filter_size);
            block_filters.emplace_back(filter);
        }
        printf("Created %ld block filters, total size %ld \n", block_filters.size(), total_block_filter_size);
    }

    uint64_t CreateBlockRangeFilters(uint16_t block_range_size) {
        printf("Creating block-range filters, %d (size: %ld) ...\n", block_range_size, size());
        std::vector<GCSFilter> filters;
        filters.reserve(1 + size() / block_range_size);
        uint64_t total_range_filter_size = 0;
        for (size_t i = SKIP_BLOCKS; i < size(); i += block_range_size) {
            BlockRangeFilterBuilder builder(this->scriptpool);
            for (size_t j = 0; j < block_range_size && i + j < size(); ++j) {
                builder.AddBlock(GetBlock(i + j));
            }
            const auto filter = builder.Finish();
            auto filter_size = filter.GetEncoded().size();
            total_range_filter_size += filter_size;
            // printf("%ld: filter size: %ld\n", i, filter_size);
            filters.emplace_back(filter);
        }
        block_range_filter_sets[block_range_size] = filters;

        printf("Range %d, Created %ld block range filters, total size %ld \n",
            block_range_size, filters.size(), total_range_filter_size);
        return total_range_filter_size;
    }

    SimulationResult RunBlockFilterSimulation(uint64_t script_idx) const {
        printf("Running block filter simulation ... \n");
        Script script = scriptpool.GetScript(script_idx);
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_txs{0};
        size_t total_dl{0};
        // Check each block filter
        for (uint32_t h = SKIP_BLOCKS; h < size(); ++h) {
            const auto& block_filter = this->block_filters[h];
            total_dl += block_filter.GetEncoded().size();
            // printf("h %d \n", h);
            if (block_filter.Match(script)) {
                // filter matches, we download & check the block
                // printf("Block filter height = %d matched!", h);
                ++block_filter_matches;
                total_dl += GetBlock(h).GetRoughSize();
                // Obtain transactions
                const auto txs = GetBlock(h).GetTxsByScript(script_idx);
                if (txs.empty()) {
                    printf("Block filter height = %d matched but negative! \n", h);
                    ++negative_block_filter_matches;
                } else {
                    printf("Block filter height = %d matched (positive, %ld txs)! \n", h, txs.size());
                }
                total_txs += txs.size();
            }
        }
        printf("Block filter simulation done, dl_size %ld filter matches %ld  negative %ld pos_blocks %ld pos_txs %ld \n",
            total_dl, block_filter_matches, negative_block_filter_matches, block_filter_matches - negative_block_filter_matches, total_txs);

        return SimulationResult {
            .block_range_size = 1,
            .total_filter_size = total_block_filter_size,
            .total_dl = total_dl,
        };
    }

    SimulationResult RunBlockRangeSimulation(uint16_t block_range_size, ScriptIdx script_idx) const {
        printf("Running block filter simulation, Range size: %d ... \n", block_range_size);
        Script script = scriptpool.GetScript(script_idx);
        const auto& filters = this->block_range_filter_sets.find(block_range_size)->second;
        size_t block_range_filter_matches{0};
        size_t negative_block_range_filter_matches{0};
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_filters{0};
        size_t total_txs{0};
        size_t total_dl{0};
        // Check each block RANGE filter
        for (uint32_t h = SKIP_BLOCKS; h < size(); h += block_range_size) {
            const auto& block_range_filter = filters[h / block_range_size];
            total_dl += block_range_filter.GetEncoded().size();
            // printf("h %d \n", h);
            if (block_range_filter.Match(script)) {
                // range filter matches, we download & check each block filter
                // printf("Range block filter height = %d - %d (%d) matched! \n", h, h + block_range_size - 1, h / block_range_size);
                ++block_range_filter_matches;

                size_t filters2{0};
                size_t txs2{0};
                for (uint32_t h2 = h; h2 < h + block_range_size && h2 < size(); ++h2) {
                    const auto& block_filter = this->block_filters[h2];
                    total_dl += block_filter.GetEncoded().size();
                    // printf("h2 %d \n", h2);
                    if (block_filter.Match(script)) {
                        // filter matches, we download & check the block
                        // printf("Block filter height = %d matched!", h);
                        ++block_filter_matches;
                        total_dl += GetBlock(h2).GetRoughSize();
                        // Obtain transactions
                        const auto txs = GetBlock(h2).GetTxsByScript(script_idx);
                        if (txs.empty()) {
                            printf("Block filter height = %d matched but negative! \n", h2);
                            ++negative_block_filter_matches;
                        } else {
                            printf("Block filter height = %d matched (positive, %ld txs)! \n", h2, txs.size());
                            ++total_filters;
                            ++filters2;
                        }
                        txs2 += txs.size();
                    }

                    total_dl += GetBlock(h).GetRoughSize();
                } // h2

                if (txs2 == 0) {
                    printf("Range block filter height = %d - %d (%d) matched but negative! \n", h, h + block_range_size - 1, h / block_range_size);
                    ++negative_block_range_filter_matches;
                } else {
                    printf("Range block filter height = %d - %d (%d) matched (positive, %ld filters, %ld txs)! \n", h, h + block_range_size - 1, h / block_range_size, filters2, txs2);
                }
                total_txs += txs2;
            }
        }
        printf("Block range filter simulation done %d, dl_size %ld range filter matches %ld negative %ld   filter matches %ld negative %ld  pos_txs %ld \n",
            block_range_size, total_dl, block_range_filter_matches, negative_block_range_filter_matches, block_filter_matches, negative_block_filter_matches, total_txs);

        return SimulationResult {
            .block_range_size = block_range_size,
            .total_filter_size = 0, // Not available here
            .total_dl = 0, // TODO
        };
    }
}; // BlockChainManager

Transaction::Transaction(const Blockchain& chain, const ScriptPool& scriptpool, const InputPool& inputpool, std::vector<InputIdx> inputs, std::vector<Output> outputs) {
    this->inputs.clear();
    for (const auto& inp_idx : inputs) {
        const auto& inp = inputpool.GetInput(inp_idx);
        if (!chain.HasInput(inp)) {
            printf("Tx::Tx HasInput failed! "); inp.print(); printf(" \n");
            assert(false);
        }
        this->inputs.emplace_back(inp_idx);
    }
    this->outputs.clear();
    for (const auto& o : outputs) {
        this->outputs.emplace_back(o);
    }

    // compute size
    size_t size = HEADER_SIZE + this->inputs.size() * INPUT_SIZE;
    for (const auto& o: outputs) {
        const auto& script = scriptpool.GetScript(o.ScIdx());
        size += 8 + script.size();
    }
    this->rough_size = size;
}

/*
// Check if transaction matches a script (input or output)
bool Transaction::MatchesScript(const Script& script) const {
    for (auto& out: outputs) {
        if (out.outscript == script) {
            return true;
        }
    }
    for (auto& in: inputs) {
        const auto& out = chain.GetOutput(in.block_height, in.tx_index, in.output_index);
        if (out.outscript == script) {
            return true;
        }
    }
    return false;
}
*/

Block::Block(const Blockchain& chain, const InputPool& inputpool, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng) {
    this->height = height;
    SetRandomBlockHash(rng);

    txs = transactions;
    this->total_rough_size = 0;

    this->scripts.clear();
    this->scripts.reserve(DEFAULT_SCRIPTS_PER_BLOCK);
    for (size_t ti = 0; ti < this->txs.size(); ++ti) {
        const auto& tx = this->txs[ti];
        for (const auto& inp_idx : tx.GetInputs()) {
            const auto& inp = inputpool.GetInput(inp_idx);
            const auto& script = chain.GetOutput(inp.block_height, inp.tx_index, inp.output_index);
            this->scripts.emplace_back(ti, true, script.ScIdx());
        }
        for (const auto& o : tx.GetOutputs()) {
            this->scripts.emplace_back(ti, false, o.ScIdx());
        }
        this->total_rough_size += tx.GetRoughSize();
    }
}

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
    for (uint64_t i = 0; i < BLOCK_COUNT; ++i) {
        manager.AddNewBlock(TX_PER_BLOCK);
    }

    // manager.AnalyzeScriptFrequencies();

    // Script dummy_script = manager.PickScriptWithDesiredBlockOccurance(1, 10);

    manager.CreateBlockFilters();
    printf("Result Range 1 Count %ld Total filter size: %ld\n", manager.BlockFiltersCount(), manager.BlockFiltersTotalSize());

    std::vector<uint16_t> block_range_sizes{4, 16, 64, 256, 1024, 2016};

    for (auto block_range_size: block_range_sizes) {
        auto total_size = manager.CreateBlockRangeFilters(block_range_size);
        double ratio = 100.0 * (double)total_size / (double)manager.BlockFiltersTotalSize();
        printf("Result Range %d Total range filter size: %ld Ratio: %g \n",
            block_range_size, total_size, ratio);
    }

    std::vector<uint64_t> scripts3;
    for (auto i = 0; i < 6; ++i) {
        scripts3.emplace_back(manager.PickScriptIndexWithDesiredBlockOccurance(3, 6));
    }

    for (const auto& script: scripts3) {
        auto result1 = manager.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts3) {
            auto result = manager.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    std::vector<uint64_t> scripts20;
    for (auto i = 0; i < 6; ++i) {
        scripts20.emplace_back(manager.PickScriptIndexWithDesiredBlockOccurance(20, 30));
    }

    for (const auto& script: scripts20) {
        auto result1 = manager.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts20) {
            auto result = manager.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    // printf("scriptpool %ld\n", scriptpool.size());
    // for (auto i = 0; i < 20; ++i) {
    //     printf("  %ld\n", scriptpool.PickIndexWithSkewedProb(m_rng));
    // }
}

BOOST_AUTO_TEST_SUITE_END()

