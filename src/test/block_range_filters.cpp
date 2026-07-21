// Copyright (c) 2026-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// cmake --build build -t test_bitcoin -j4 && ./build/bin/test_bitcoin -t block_range_filters
//

#include <blockfilter.h>
#include <uint256.h>
#include <test/util/setup_common.h>

#include <cassert>
#include <fstream>
#include <optional>
#include <tuple>

#include <boost/test/unit_test.hpp>

const uint64_t BLOCK_COUNT = 30'000;
// const uint64_t BLOCK_COUNT = 5'000;
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

typedef uint32_t ScriptIdx;

class ScriptPool {
    std::vector<Script> scripts;
    FastRandomContext& rng;

public:
    static constexpr size_t SCRIPT_POOL_SIZE = 1'000'000;

    ScriptPool(FastRandomContext& rng) : rng(rng) {}

    void Generate(size_t size = SCRIPT_POOL_SIZE) {
        scripts.resize(size);
        for (size_t i = 0; i < size; ++i) {
            scripts[i] = RandomScript(rng);
        }
    }

    //! Write scripts to a binary file: uint64 count, then per script: uint32 len + bytes.
    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        uint64_t count = scripts.size();
        f.write(reinterpret_cast<const char*>(&count), sizeof(count));
        for (const auto& script : scripts) {
            uint32_t len = script.size();
            f.write(reinterpret_cast<const char*>(&len), sizeof(len));
            f.write(reinterpret_cast<const char*>(script.data()), len);
        }
    }

    //! Read scripts from a binary file written by Write(). Returns false on failure.
    bool Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint64_t count;
        f.read(reinterpret_cast<char*>(&count), sizeof(count));
        if (!f) return false;
        scripts.resize(count);
        for (uint64_t i = 0; i < count; ++i) {
            uint32_t len;
            f.read(reinterpret_cast<char*>(&len), sizeof(len));
            if (!f) return false;
            scripts[i].resize(len);
            f.read(reinterpret_cast<char*>(scripts[i].data()), len);
            if (!f) return false;
        }
        return true;
    }

    const Script& GetScript(ScriptIdx index) const {
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
    uint16_t tx_index;
    uint8_t output_index;
    ScriptIdx script_idx{0};  // script of the output this input spends

    bool operator<(const Input& other) const
    {
        return std::tie(block_height, tx_index, output_index) <
               std::tie(other.block_height, other.tx_index, other.output_index);
    }

    void print() const {
        printf("h %d  tx %d  out %d  sc %d", block_height, tx_index, output_index, script_idx);
    }
};

class Output {
    //! script index
    ScriptIdx script_idx;

public:
    Output(ScriptIdx script_idx) : script_idx(script_idx) {}

    ScriptIdx ScIdx() const { return script_idx; }
};


class Blockchain;

class Transaction {
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    uint32_t rough_size;

    Transaction() : rough_size(0) {}

public:
    Transaction(const ScriptPool& scriptpool, std::vector<Input> inputs, std::vector<Output> outputs);

    void WriteTo(std::ostream& f) const {
        f.write(reinterpret_cast<const char*>(&rough_size), sizeof(rough_size));
        uint16_t ni = inputs.size();
        f.write(reinterpret_cast<const char*>(&ni), sizeof(ni));
        for (const auto& inp : inputs) {
            f.write(reinterpret_cast<const char*>(&inp.block_height), sizeof(inp.block_height));
            f.write(reinterpret_cast<const char*>(&inp.tx_index),     sizeof(inp.tx_index));
            f.write(reinterpret_cast<const char*>(&inp.output_index), sizeof(inp.output_index));
            f.write(reinterpret_cast<const char*>(&inp.script_idx),   sizeof(inp.script_idx));
        }
        uint16_t no = outputs.size();
        f.write(reinterpret_cast<const char*>(&no), sizeof(no));
        for (const auto& out : outputs) {
            ScriptIdx si = out.ScIdx();
            f.write(reinterpret_cast<const char*>(&si), sizeof(si));
        }
    }

    static Transaction ReadFrom(std::ifstream& f) {
        Transaction tx;
        f.read(reinterpret_cast<char*>(&tx.rough_size), sizeof(tx.rough_size));
        uint16_t ni;
        f.read(reinterpret_cast<char*>(&ni), sizeof(ni));
        tx.inputs.resize(ni);
        for (auto& inp : tx.inputs) {
            f.read(reinterpret_cast<char*>(&inp.block_height), sizeof(inp.block_height));
            f.read(reinterpret_cast<char*>(&inp.tx_index),     sizeof(inp.tx_index));
            f.read(reinterpret_cast<char*>(&inp.output_index), sizeof(inp.output_index));
            f.read(reinterpret_cast<char*>(&inp.script_idx),   sizeof(inp.script_idx));
        }
        uint16_t no;
        f.read(reinterpret_cast<char*>(&no), sizeof(no));
        tx.outputs.reserve(no);
        for (uint16_t i = 0; i < no; ++i) {
            ScriptIdx si;
            f.read(reinterpret_cast<char*>(&si), sizeof(si));
            tx.outputs.emplace_back(si);
        }
        return tx;
    }

    bool HasOutIndex(uint8_t outindex) const {
        return outindex < outputs.size();
    }

    // Check if transaction matches a script (input or output)
    // bool MatchesScript(const Script& script) const;

    uint32_t GetRoughSize() const { return rough_size; }

    Output GetOutput(uint8_t outindex) const {
        assert(outindex < outputs.size());
        return this->outputs[outindex];
    }

    const std::vector<Input>& GetInputs() const { return this->inputs; }
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
    uint32_t total_rough_size;

public:
    static constexpr size_t DEFAULT_SCRIPTS_PER_BLOCK = 4500;

    Block() : height(0), total_rough_size(0) {}

    Block(uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng);

    void WriteTo(std::ostream& f) const {
        f.write(reinterpret_cast<const char*>(&height),           sizeof(height));
        f.write(reinterpret_cast<const char*>(blockhash.begin()), 32);
        f.write(reinterpret_cast<const char*>(&total_rough_size), sizeof(total_rough_size));
        uint32_t nt = txs.size();
        f.write(reinterpret_cast<const char*>(&nt), sizeof(nt));
        for (const auto& tx : txs) tx.WriteTo(f);
        uint32_t ns = scripts.size();
        f.write(reinterpret_cast<const char*>(&ns), sizeof(ns));
        for (const auto& sc : scripts) {
            uint8_t is_input = sc.is_input ? 1 : 0;
            f.write(reinterpret_cast<const char*>(&sc.txindex),  sizeof(sc.txindex));
            f.write(reinterpret_cast<const char*>(&is_input),    sizeof(is_input));
            f.write(reinterpret_cast<const char*>(&sc.script_idx), sizeof(sc.script_idx));
        }
    }

    static Block ReadFrom(std::ifstream& f) {
        Block b;
        f.read(reinterpret_cast<char*>(&b.height),           sizeof(b.height));
        f.read(reinterpret_cast<char*>(b.blockhash.begin()), 32);
        f.read(reinterpret_cast<char*>(&b.total_rough_size), sizeof(b.total_rough_size));
        uint32_t nt;
        f.read(reinterpret_cast<char*>(&nt), sizeof(nt));
        b.txs.reserve(nt);
        for (uint32_t i = 0; i < nt; ++i) b.txs.emplace_back(Transaction::ReadFrom(f));
        uint32_t ns;
        f.read(reinterpret_cast<char*>(&ns), sizeof(ns));
        b.scripts.reserve(ns);
        for (uint32_t i = 0; i < ns; ++i) {
            uint16_t txindex;
            uint8_t  is_input;
            ScriptIdx script_idx;
            f.read(reinterpret_cast<char*>(&txindex),    sizeof(txindex));
            f.read(reinterpret_cast<char*>(&is_input),   sizeof(is_input));
            f.read(reinterpret_cast<char*>(&script_idx), sizeof(script_idx));
            b.scripts.emplace_back(txindex, is_input != 0, script_idx);
        }
        return b;
    }

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

    Output GetOutput(uint16_t txindex, uint8_t outindex) const {
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

    uint32_t GetRoughSize() const { return total_rough_size; }
};

class Blockchain {
    std::vector<Block> blocks;
    uint32_t total_height{0};  // total blocks ever added, including those flushed from memory

public:
    static constexpr size_t NUM_BLOCKS = 300'000;

    Blockchain() {}

    void AddBlock(const Block& block) {
        assert(total_height == block.height);
        blocks.emplace_back(block);
        ++total_height;
    }

    //! Clear in-memory blocks while preserving the logical total_height.
    void Flush() {
        blocks.clear();
    }

    //! Returns true if height is currently held in memory (not flushed).
    bool HasBlock(uint32_t height) const {
        uint32_t base = total_height - (uint32_t)blocks.size();
        return height >= base && height < total_height;
    }

    size_t size() const { return total_height; }

    //! Only valid for blocks still in memory (asserts otherwise).
    const Block& GetBlock(uint32_t height) const {
        uint32_t base = total_height - (uint32_t)blocks.size();
        assert(height >= base && height < total_height);
        return blocks[height - base];
    }

    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        f.write(reinterpret_cast<const char*>(&total_height), sizeof(total_height));
        for (const auto& block : blocks) block.WriteTo(f);
    }

    //! Read count only (fast resume — no blocks loaded into memory).
    bool ReadCount(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return false;
        total_height = n;
        blocks.clear();
        return true;
    }

    bool Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return false;
        blocks.clear();
        blocks.reserve(n);
        printf("Reading blockchain file ... (%d)\n", n);
        for (uint32_t i = 0; i < n; ++i) {
            blocks.emplace_back(Block::ReadFrom(f));
            if (!f) return false;
        }
        total_height = n;
        return true;
    }

    //! Append all in-memory blocks to the file, updating the total count header.
    //! Falls back to Write() if the file does not exist yet.
    void Append(const std::string& path) const {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) {
            Write(path);
            return;
        }
        f.seekp(0);
        f.write(reinterpret_cast<const char*>(&total_height), sizeof(total_height));
        f.seekp(0, std::ios::end);
        for (const auto& block : blocks) {
            block.WriteTo(f);
        }
    }
};

class UtxoSet {
    std::vector<Input> vector_of_utxos;
    std::map<Input, size_t> map_of_indices;
    // size_t next_index;

public:
    UtxoSet() : vector_of_utxos(), map_of_indices() {}

    void Add(Input input) {
        auto index = vector_of_utxos.size();
        vector_of_utxos.emplace_back(input);
        map_of_indices[input] = index;
    }

    void Remove(Input input) {
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

    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        uint32_t n = vector_of_utxos.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        for (const auto& inp : vector_of_utxos) {
            f.write(reinterpret_cast<const char*>(&inp.block_height), sizeof(inp.block_height));
            f.write(reinterpret_cast<const char*>(&inp.tx_index),     sizeof(inp.tx_index));
            f.write(reinterpret_cast<const char*>(&inp.output_index), sizeof(inp.output_index));
            f.write(reinterpret_cast<const char*>(&inp.script_idx),   sizeof(inp.script_idx));
        }
    }

    bool Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return false;
        vector_of_utxos.clear();
        map_of_indices.clear();
        vector_of_utxos.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            auto& inp = vector_of_utxos[i];
            f.read(reinterpret_cast<char*>(&inp.block_height), sizeof(inp.block_height));
            f.read(reinterpret_cast<char*>(&inp.tx_index),     sizeof(inp.tx_index));
            f.read(reinterpret_cast<char*>(&inp.output_index), sizeof(inp.output_index));
            f.read(reinterpret_cast<char*>(&inp.script_idx),   sizeof(inp.script_idx));
            if (!f) return false;
            map_of_indices[inp] = i;
        }
        return true;
    }
};

// Per-script transaction occurrence counts
class ScriptIndex {
    //! counts[script_idx] = number of tx script usages (inputs + outputs) across all blocks
    std::vector<uint32_t> counts;

public:
    ScriptIndex() {
        counts.resize(ScriptPool::SCRIPT_POOL_SIZE, 0);
    }

    void Add(ScriptIdx script_idx) {
        assert(script_idx < counts.size());
        //if (script_idx >= counts.size()) counts.resize(script_idx + 1, 0);
        ++counts[script_idx];
    }

    uint32_t CountTxs(ScriptIdx script_idx) const {
        //if (script_idx >= counts.size()) return 0;
        assert(script_idx <= counts.size());
        return counts[script_idx];
    }

    size_t size() const { return counts.size(); }

    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        uint32_t n = counts.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        f.write(reinterpret_cast<const char*>(counts.data()), n * sizeof(uint32_t));
    }

    bool Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return false;
        counts.resize(n);
        f.read(reinterpret_cast<char*>(counts.data()), n * sizeof(uint32_t));
        return f.good();
    }
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
    Blockchain chain;
    UtxoSet utxo_set;
    ScriptIndex script_index;
    FastRandomContext& rng;

    static constexpr const char* SCRIPTPOOL_CACHE = "scriptpool_cache.bin";
    static constexpr const char* CHAIN_CACHE      = "chain_cache.bin";
    static constexpr const char* UTXOSET_CACHE    = "utxoset_cache.bin";
    static constexpr const char* SCRIPTIDX_CACHE  = "script_index_cache.bin";
    static constexpr uint32_t    CHUNK_SIZE        = 1000;

public:
    BlockChainManager(FastRandomContext& rng) :
        scriptpool(rng), chain(), utxo_set(), script_index(),
        rng(rng)
    {
    }

    void CreateOrLoadBlocks(uint32_t block_count, size_t scriptpool_size = ScriptPool::SCRIPT_POOL_SIZE) {
        // Load or generate scriptpool
        if (!scriptpool.Read(SCRIPTPOOL_CACHE)) {
            printf("ScriptPool cache not found, generating %ld scripts...\n", scriptpool_size);
            scriptpool.Generate(scriptpool_size);
            scriptpool.Write(SCRIPTPOOL_CACHE);
            printf("ScriptPool %ld scripts written to %s\n", scriptpool.size(), SCRIPTPOOL_CACHE);
        } else {
            printf("ScriptPool %ld scripts loaded from %s\n", scriptpool.size(), SCRIPTPOOL_CACHE);
        }

        // Try to resume from a previous (possibly partial) run.
        // ReadCount reads only the header (block count) — no blocks loaded into memory.
        if (chain.ReadCount(CHAIN_CACHE) && utxo_set.Read(UTXOSET_CACHE)) {
            printf("Resumed: chain %ld blocks, utxo_set %ld entries\n", chain.size(), utxo_set.size());
        } else {
            // Nothing usable on disk — start from scratch
            chain = Blockchain{};
            utxo_set = UtxoSet{};
            AddBlock(CreateGenesisBlock());
            chain.Write(CHAIN_CACHE);
            chain.Flush();
            utxo_set.Write(UTXOSET_CACHE);
            printf("Started fresh: genesis block written\n");
        }

        // block_count + 1: genesis (height 0) + block_count generated blocks
        const uint32_t target = block_count + 1;

        if (chain.size() >= target) {
            // Chain is complete; load or rebuild script index
            if (script_index.Read(SCRIPTIDX_CACHE)) {
                printf("ScriptIndex %ld entries loaded from %s\n", script_index.size(), SCRIPTIDX_CACHE);
            } else {
                BuildScriptIndex();
                script_index.Write(SCRIPTIDX_CACHE);
                printf("ScriptIndex %ld entries written to %s\n", script_index.size(), SCRIPTIDX_CACHE);
            }
            return;
        }

        // Create remaining blocks in chunks, flushing from memory after each save.
        while (chain.size() < target) {
            uint32_t chunk_start = chain.size();
            uint32_t to_create = std::min(CHUNK_SIZE, target - (uint32_t)chain.size());
            printf("Creating blocks %d–%d ...\n", chunk_start, chunk_start + to_create - 1);
            for (uint32_t i = 0; i < to_create; ++i) {
                AddNewBlock(TX_PER_BLOCK);
            }
            printf("\n");
            chain.Append(CHAIN_CACHE);
            chain.Flush();  // free in-memory blocks; total_height is preserved
            utxo_set.Write(UTXOSET_CACHE);
            printf("Saved: chain %ld blocks, utxo_set %ld entries\n", chain.size(), utxo_set.size());
        }

        BuildScriptIndex();
        script_index.Write(SCRIPTIDX_CACHE);
        printf("ScriptIndex %ld entries written to %s\n", script_index.size(), SCRIPTIDX_CACHE);
    }

    void AddBlock(const Block& block)
    {
        // printf("Adding block %d (utxos: %ld)...\n", block.height, utxo_set.size());
        chain.AddBlock(block);
        // Update UTXOs
        for (uint16_t txindex = 0; txindex < block.txs.size(); ++txindex) {
            const auto& tx = block.txs[txindex];
            // Utxos remove spent -- Done in GenerateTxsForNextBlock
            // for (const auto& inp : tx.GetInputs()) {
                // utxo_set.Remove(inp);
            // }
            for (uint8_t outindex = 0; outindex < tx.GetOutputs().size(); ++outindex) {
                // Utxos: add new unspent
                // Store the output's script_idx in the Input so inputs carry it when spent
                ScriptIdx sc = tx.GetOutput(outindex).ScIdx();
                utxo_set.Add(Input{block.height, txindex, outindex, sc});
            }
        }
        if (block.height % 50 == 0) {
            printf("Added block %d with %ld txs, size %d ; utxos: %ld \r",
                block.height, block.txs.size(), block.GetRoughSize(), utxo_set.size());
                fflush(stdout);
        }
    }

    void BuildScriptIndex() {
        printf("Building script index (using chain from file)...\n");
        script_index = ScriptIndex{};
        ForEachBlock([this](const Block& block) {
            for (const auto& sc : block.scripts) {
                script_index.Add(sc.script_idx);
            }
        });
        printf("Script index built: %ld scripts\n", script_index.size());
    }

    Transaction CreateGenesisTx() const {
        auto script_index1 = scriptpool.PickIndexWithSkewedProb();
        return Transaction(scriptpool, {}, {Output(script_index1)});
    }

    Block CreateGenesisBlock() const {
        auto tx1 = CreateGenesisTx();
        return Block(0, {tx1}, rng);
    }

    std::vector<Transaction> GenerateTxsForNextBlock(size_t desired_tx_num)
    {
        auto num = std::max(std::min(utxo_set.size() / 3, desired_tx_num), size_t(1));
        std::vector<Transaction> txs;
        txs.reserve(num);
        txs.emplace_back(CreateGenesisTx());
        for (size_t i = 0; i < num; ++i) {
            if (utxo_set.size() == 0) break;
            // TODO more flexible input and output count
            std::vector<Input> inputs;
            auto numinputs = std::min(size_t(2), utxo_set.size());
            for (size_t i = 0; i < numinputs; ++i) {
                inputs.emplace_back(utxo_set.PickAndRemoveRandom(rng));
            }
            std::vector<Output> outputs;
            auto numoutputs = 2;
            for (auto i = 0; i < numoutputs; ++i) {
                const auto output_script_index = scriptpool.PickIndexWithSkewedProb();
                outputs.emplace_back(Output(output_script_index));
            }
            auto tx = Transaction(scriptpool, inputs, outputs);
            txs.emplace_back(tx);
        }
        return txs;
    }

    void AddNewBlock(size_t desired_tx_num) {
        auto height = chain.size();
        auto txs = GenerateTxsForNextBlock(desired_tx_num);
        auto block = Block(height, txs, rng);
        AddBlock(block);
    }

    size_t size() const { return chain.size(); }

    const Block& GetBlock(uint32_t height) const { return chain.GetBlock(height); }

    const ScriptPool& GetScriptPool() const { return scriptpool; }

    void AnalyzeScriptFrequencies() const {
        const int n = 1000;
        printf("Analyzing script occurence counts %d ... \n", n);
        for (auto i = 0; i < n; ++i) {
            const auto script_idx = scriptpool.PickIndexWithSkewedProb();
            const auto count = script_index.CountTxs(script_idx);
            printf("  c %d \n", count);
        }
    }

    ScriptIdx PickScriptIndexWithDesiredTxOccurance(size_t min, size_t max) const {
        size_t tries = 0;
        while (tries < 1'000'000) {
            const auto script_idx = scriptpool.PickIndexWithSkewedProb();
            const auto count = script_index.CountTxs(script_idx);
            if (count >= min && count <= max) {
                printf("Picked a script with %d tx occurance (%ld -- %ld, %ld tries) \n", count, min, max, tries+1);
                return script_idx;
            }
            ++tries;
        }
        assert(false); // not found in time
        return scriptpool.PickIndexWithSkewedProb(); // fallback
    }

    //! Read blocks sequentially from the chain file, calling fn(block) for each.
    template<typename Fn>
    void ForEachBlock(Fn&& fn) const {
        std::ifstream f(CHAIN_CACHE, std::ios::binary);
        if (!f) { printf("ForEachBlock: cannot open %s\n", CHAIN_CACHE); return; }
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return;
        for (uint32_t i = 0; i < n; ++i) {
            Block block = Block::ReadFrom(f);
            if (!f) break;
            fn(block);
            if (i % 100 == 99) {
                printf("  [%d/%d]\r", i + 1, n);
                fflush(stdout);
            }
        }
        printf("  [%d/%d]\n", n, n);
    }

}; // BlockChainManager

class FilterManager {
    const BlockChainManager& chain_mgr;
    // One filter per block
    std::vector<GCSFilter> block_filters;
    uint64_t total_block_filter_size{0};
    // Range filters keyed by range size
    std::map<uint16_t, std::vector<GCSFilter>> block_range_filter_sets;

public:
    FilterManager(const BlockChainManager& chain_mgr) : chain_mgr(chain_mgr) {}

    uint64_t BlockFiltersCount() const { return block_filters.size(); }
    uint64_t BlockFiltersTotalSize() const { return total_block_filter_size; }

    void CreateBlockFilters() {
        total_block_filter_size = 0;
        printf("Creating block filters (using chain from file)...\n");
        chain_mgr.ForEachBlock([this](const Block& block) {
            if (block.height < SKIP_BLOCKS) return;
            const auto filter = BuildFilterForBlock(block, chain_mgr.GetScriptPool());
            total_block_filter_size += filter.GetEncoded().size();
            block_filters.emplace_back(filter);
        });
        printf("Created %ld block filters, total size %ld \n", block_filters.size(), total_block_filter_size);
    }

    uint64_t CreateBlockRangeFilters(uint16_t block_range_size) {
        printf("Creating block-range filters, %d (using chain from file) ...\n", block_range_size);
        std::vector<GCSFilter> filters;
        uint64_t total_range_filter_size = 0;

        std::optional<BlockRangeFilterBuilder> builder;
        uint32_t count_in_range = 0;

        auto finalize_range = [&]() {
            if (!builder) return;
            const auto filter = builder->Finish();
            total_range_filter_size += filter.GetEncoded().size();
            filters.emplace_back(filter);
            builder.reset();
            count_in_range = 0;
        };

        chain_mgr.ForEachBlock([&](const Block& block) {
            if (block.height < SKIP_BLOCKS) return;
            if (!builder) builder.emplace(chain_mgr.GetScriptPool());
            builder->AddBlock(block);
            if (++count_in_range == block_range_size) finalize_range();
        });
        finalize_range(); // flush any partial final range

        block_range_filter_sets[block_range_size] = std::move(filters);
        printf("Range %d, Created %ld block range filters, total size %ld \n",
            block_range_size, block_range_filter_sets[block_range_size].size(), total_range_filter_size);
        return total_range_filter_size;
    }

    SimulationResult RunBlockFilterSimulation(ScriptIdx script_idx) const {
        printf("Running block filter simulation ... \n");
        Script script = chain_mgr.GetScriptPool().GetScript(script_idx);
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_txs{0};
        size_t total_dl{0};
        chain_mgr.ForEachBlock([&](const Block& block) {
            uint32_t h = block.height;
            if (h < SKIP_BLOCKS) return;
            uint32_t fi = h - (uint32_t)SKIP_BLOCKS;
            const auto& block_filter = this->block_filters[fi];
            total_dl += block_filter.GetEncoded().size(); // DL block filter
            if (block_filter.Match(script)) {
                // filter matches, we download & check the block
                // printf("Block filter height = %d matched!", h);
                ++block_filter_matches;
                total_dl += block.GetRoughSize(); // DL block if block filter match
                const auto txs = block.GetTxsByScript(script_idx);
                if (txs.empty()) {
                    printf("Block filter height = %d matched but negative! \n", h);
                    ++negative_block_filter_matches;
                } else {
                    printf("Block filter height = %d matched (positive, %ld txs)! \n", h, txs.size());
                }
                total_txs += txs.size();
            }
        });
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
        Script script = chain_mgr.GetScriptPool().GetScript(script_idx);
        const auto& filters = this->block_range_filter_sets.find(block_range_size)->second;
        size_t block_range_filter_matches{0};
        size_t negative_block_range_filter_matches{0};
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_filters{0};
        size_t total_txs{0};
        size_t total_dl{0};

        int32_t cur_range_idx = -1;
        bool cur_range_matched = false;
        size_t cur_range_txs = 0;
        size_t cur_range_filters = 0;
        uint32_t cur_range_start = 0;

        auto finalize_range = [&]() {
            if (cur_range_idx < 0 || !cur_range_matched) return;
            if (cur_range_txs == 0) {
                printf("Range block filter height = %d - %d (%d) matched but negative! \n",
                    cur_range_start, cur_range_start + block_range_size - 1, cur_range_idx);
                ++negative_block_range_filter_matches;
            } else {
                printf("Range block filter height = %d - %d (%d) matched (positive, %ld filters, %ld txs)! \n",
                    cur_range_start, cur_range_start + block_range_size - 1, cur_range_idx, cur_range_filters, cur_range_txs);
            }
        };

        chain_mgr.ForEachBlock([&](const Block& block) {
            uint32_t h = block.height;
            if (h < SKIP_BLOCKS) return;
            uint32_t fi = h - (uint32_t)SKIP_BLOCKS;
            int32_t range_idx = (int32_t)(fi / block_range_size);

            if (range_idx != cur_range_idx) {
                finalize_range();
                cur_range_idx = range_idx;
                cur_range_txs = 0;
                cur_range_filters = 0;
                cur_range_start = h;
                const auto& block_range_filter = filters[range_idx];
                total_dl += block_range_filter.GetEncoded().size(); // DL block-range filter
                cur_range_matched = block_range_filter.Match(script);
                if (cur_range_matched) ++block_range_filter_matches;
            }

            if (!cur_range_matched) return;

            const auto& block_filter = this->block_filters[fi];
            total_dl += block_filter.GetEncoded().size(); // DL block filter if block-range filter match
            if (block_filter.Match(script)) {
                ++block_filter_matches;
                total_dl += block.GetRoughSize(); // DL block if block filter match
                const auto txs = block.GetTxsByScript(script_idx);
                if (txs.empty()) {
                    printf("Block filter height = %d matched but negative! \n", h);
                    ++negative_block_filter_matches;
                } else {
                    printf("Block filter height = %d matched (positive, %ld txs)! \n", h, txs.size());
                    ++total_filters;
                    ++cur_range_filters;
                }
                cur_range_txs += txs.size();
                total_txs += txs.size();
            }
        });
        finalize_range();

        printf("Block range filter simulation done %d, dl_size %ld range filter matches %ld negative %ld   filter matches %ld negative %ld  pos_txs %ld \n",
            block_range_size, total_dl, block_range_filter_matches, negative_block_range_filter_matches, block_filter_matches, negative_block_filter_matches, total_txs);
        return SimulationResult {
            .block_range_size = block_range_size,
            .total_filter_size = 0,
            .total_dl = 0,
        };
    }
}; // FilterManager

Transaction::Transaction(const ScriptPool& scriptpool, std::vector<Input> inputs, std::vector<Output> outputs) {
    this->inputs = std::move(inputs);
    this->outputs = std::move(outputs);

    size_t size = HEADER_SIZE + this->inputs.size() * INPUT_SIZE;
    for (const auto& o : this->outputs) {
        size += 8 + scriptpool.GetScript(o.ScIdx()).size();
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

Block::Block(uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng) {
    this->height = height;
    SetRandomBlockHash(rng);

    txs = std::move(transactions);
    this->total_rough_size = 0;

    this->scripts.clear();
    this->scripts.reserve(DEFAULT_SCRIPTS_PER_BLOCK);
    for (size_t ti = 0; ti < this->txs.size(); ++ti) {
        const auto& tx = this->txs[ti];
        for (const auto& inp : tx.GetInputs()) {
            // inp.script_idx is set when the UTXO was created; no chain lookup needed
            this->scripts.emplace_back(ti, true, inp.script_idx);
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
    auto manager = BlockChainManager(m_rng);
    manager.CreateOrLoadBlocks(BLOCK_COUNT, ScriptPool::SCRIPT_POOL_SIZE);

    // manager.AnalyzeScriptFrequencies();

    // Script dummy_script = manager.PickScriptWithDesiredBlockOccurance(1, 10);

    auto filter_mgr = FilterManager(manager);
    filter_mgr.CreateBlockFilters();
    printf("Result Range 1 Count %ld Total filter size: %ld\n", filter_mgr.BlockFiltersCount(), filter_mgr.BlockFiltersTotalSize());

    std::vector<uint16_t> block_range_sizes{4, 16, 64, 256, 1024, 2016};

    for (auto block_range_size: block_range_sizes) {
        auto total_size = filter_mgr.CreateBlockRangeFilters(block_range_size);
        double ratio = 100.0 * (double)total_size / (double)filter_mgr.BlockFiltersTotalSize();
        printf("Result Range %d Total range filter size: %ld Ratio: %g \n",
            block_range_size, total_size, ratio);
    }

    std::vector<ScriptIdx> scripts3;
    for (auto i = 0; i < 6; ++i) {
        scripts3.emplace_back(manager.PickScriptIndexWithDesiredTxOccurance(3, 6));
    }

    for (const auto& script: scripts3) {
        (void)filter_mgr.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts3) {
            (void)filter_mgr.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    std::vector<ScriptIdx> scripts20;
    for (auto i = 0; i < 6; ++i) {
        scripts20.emplace_back(manager.PickScriptIndexWithDesiredTxOccurance(20, 30));
    }

    for (const auto& script: scripts20) {
        (void)filter_mgr.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts20) {
            (void)filter_mgr.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    // printf("scriptpool %ld\n", scriptpool.size());
    // for (auto i = 0; i < 20; ++i) {
    //     printf("  %ld\n", scriptpool.PickIndexWithSkewedProb(m_rng));
    // }
}

BOOST_AUTO_TEST_SUITE_END()

