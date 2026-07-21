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
#include <tuple>

#include <boost/test/unit_test.hpp>

// const uint64_t BLOCK_COUNT = 20'000;
const uint64_t BLOCK_COUNT = 10'000;
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


class Blockchain;

class Transaction {
    std::vector<Input> inputs;
    std::vector<Output> outputs;
    uint32_t rough_size;

    Transaction() : rough_size(0) {}

public:
    Transaction(const Blockchain& chain, const ScriptPool& scriptpool, std::vector<Input> inputs, std::vector<Output> outputs);

    void WriteTo(std::ostream& f) const {
        f.write(reinterpret_cast<const char*>(&rough_size), sizeof(rough_size));
        uint16_t ni = inputs.size();
        f.write(reinterpret_cast<const char*>(&ni), sizeof(ni));
        for (const auto& inp : inputs) {
            f.write(reinterpret_cast<const char*>(&inp.block_height), sizeof(inp.block_height));
            f.write(reinterpret_cast<const char*>(&inp.tx_index),     sizeof(inp.tx_index));
            f.write(reinterpret_cast<const char*>(&inp.output_index), sizeof(inp.output_index));
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

    Block(const Blockchain& chain, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng);

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

    Output GetOutput(uint32_t height, uint16_t txindex, uint8_t outindex) const {
        assert(height < blocks.size());
        const Block& block = this->blocks[height];
        return block.GetOutput(txindex, outindex);
    }

    size_t size() const { return blocks.size(); }

    const Block& GetBlock(uint32_t height) const {
        return blocks[height];
    }

    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        uint32_t n = blocks.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        for (const auto& block : blocks) block.WriteTo(f);
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
        return true;
    }

    //! Append blocks[from_height..end] to an existing file, updating the count at position 0.
    //! Falls back to Write() if the file does not exist yet.
    void Append(const std::string& path, uint32_t from_height) const {
        std::fstream f(path, std::ios::binary | std::ios::in | std::ios::out);
        if (!f) {
            Write(path);
            return;
        }
        uint32_t n = blocks.size();
        f.seekp(0);
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        f.seekp(0, std::ios::end);
        for (uint32_t i = from_height; i < blocks.size(); ++i) {
            blocks[i].WriteTo(f);
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
            if (!f) return false;
            map_of_indices[inp] = i;
        }
        return true;
    }
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

    void Write(const std::string& path) const {
        std::ofstream f(path, std::ios::binary);
        uint32_t n = index.size();
        f.write(reinterpret_cast<const char*>(&n), sizeof(n));
        for (const auto& [script_idx, heights] : index) {
            f.write(reinterpret_cast<const char*>(&script_idx), sizeof(script_idx));
            uint32_t nh = heights.size();
            f.write(reinterpret_cast<const char*>(&nh), sizeof(nh));
            for (uint32_t h : heights) {
                f.write(reinterpret_cast<const char*>(&h), sizeof(h));
            }
        }
    }

    bool Read(const std::string& path) {
        std::ifstream f(path, std::ios::binary);
        if (!f) return false;
        uint32_t n;
        f.read(reinterpret_cast<char*>(&n), sizeof(n));
        if (!f) return false;
        index.clear();
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t script_idx;
            f.read(reinterpret_cast<char*>(&script_idx), sizeof(script_idx));
            if (!f) return false;
            uint32_t nh;
            f.read(reinterpret_cast<char*>(&nh), sizeof(nh));
            if (!f) return false;
            auto& heights = index[script_idx];
            heights.reserve(nh);
            for (uint32_t j = 0; j < nh; ++j) {
                uint32_t h;
                f.read(reinterpret_cast<char*>(&h), sizeof(h));
                if (!f) return false;
                heights.insert(h);
            }
        }
        return true;
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

        // Try to resume from a previous (possibly partial) run
        if (chain.Read(CHAIN_CACHE) && utxo_set.Read(UTXOSET_CACHE)) {
            printf("Resumed: chain %ld blocks, utxo_set %ld entries\n", chain.size(), utxo_set.size());
        } else {
            // Nothing usable on disk — start from scratch
            chain = Blockchain{};
            utxo_set = UtxoSet{};
            AddBlock(CreateGenesisBlock());
            chain.Write(CHAIN_CACHE);
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

        // Create remaining blocks in chunks, saving after each
        while (chain.size() < target) {
            uint32_t chunk_start = chain.size();
            uint32_t to_create = std::min(CHUNK_SIZE, target - (uint32_t)chain.size());
            printf("Creating blocks %d–%d ...\n", chunk_start, chunk_start + to_create - 1);
            for (uint32_t i = 0; i < to_create; ++i) {
                AddNewBlock(TX_PER_BLOCK);
            }
            chain.Append(CHAIN_CACHE, chunk_start);
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
                utxo_set.Add(Input{block.height, txindex, outindex});
            }
        }
        if (block.height % 50 == 0) {
            printf("Added block %d with %ld txs, size %d ; utxos: %ld \n",
                block.height, block.txs.size(), block.GetRoughSize(), utxo_set.size());
        }
    }

    void BuildScriptIndex() {
        printf("Building script index...\n");
        script_index = ScriptIndex{};
        for (uint32_t h = 0; h < chain.size(); ++h) {
            const auto& block = chain.GetBlock(h);
            for (const auto& sc : block.scripts) {
                script_index.Add(sc.script_idx, h);
            }
        }
        printf("Script index built: %ld entries\n", script_index.size());
    }

    Transaction CreateGenesisTx() const {
        auto script_index1 = scriptpool.PickIndexWithSkewedProb();
        return Transaction(chain, scriptpool, {}, {Output(script_index1)});
    }

    Block CreateGenesisBlock() const {
        auto tx1 = CreateGenesisTx();
        auto genesis_block = Block(chain, 0, {tx1}, rng);
        return genesis_block;
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
            auto tx = Transaction(chain, scriptpool, inputs, outputs);
            txs.emplace_back(tx);
        }
        return txs;
    }

    void AddNewBlock(size_t desired_tx_num) {
        auto height = chain.size();
        auto txs = GenerateTxsForNextBlock(desired_tx_num);
        auto block = Block(chain, height, txs, rng);
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
            const auto count = script_index.CountBlocks(script_idx);
            printf("  c %ld \n", count);
        }
    }

    ScriptIdx PickScriptIndexWithDesiredBlockOccurance(size_t min, size_t max) const {
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
        printf("Creating block filters... (size: %ld) \n", chain_mgr.size());
        block_filters.reserve(chain_mgr.size());
        for (size_t i = SKIP_BLOCKS; i < chain_mgr.size(); ++i) {
            const auto filter = BuildFilterForBlock(chain_mgr.GetBlock(i), chain_mgr.GetScriptPool());
            auto filter_size = filter.GetEncoded().size();
            total_block_filter_size += filter_size;
            // printf("%ld: filter size: %ld\n", i, filter_size);
            block_filters.emplace_back(filter);
        }
        printf("Created %ld block filters, total size %ld \n", block_filters.size(), total_block_filter_size);
    }

    uint64_t CreateBlockRangeFilters(uint16_t block_range_size) {
        printf("Creating block-range filters, %d (size: %ld) ...\n", block_range_size, chain_mgr.size());
        std::vector<GCSFilter> filters;
        filters.reserve(1 + chain_mgr.size() / block_range_size);
        uint64_t total_range_filter_size = 0;
        for (size_t i = SKIP_BLOCKS; i < chain_mgr.size(); i += block_range_size) {
            BlockRangeFilterBuilder builder(chain_mgr.GetScriptPool());
            for (size_t j = 0; j < block_range_size && i + j < chain_mgr.size(); ++j) {
                builder.AddBlock(chain_mgr.GetBlock(i + j));
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

    SimulationResult RunBlockFilterSimulation(ScriptIdx script_idx) const {
        printf("Running block filter simulation ... \n");
        Script script = chain_mgr.GetScriptPool().GetScript(script_idx);
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_txs{0};
        size_t total_dl{0};
        // Check each block filter
        for (uint32_t h = SKIP_BLOCKS; h < chain_mgr.size(); ++h) {
            const auto& block_filter = this->block_filters[h];
            total_dl += block_filter.GetEncoded().size();
            // printf("h %d \n", h);
            if (block_filter.Match(script)) {
                // filter matches, we download & check the block
                // printf("Block filter height = %d matched!", h);
                ++block_filter_matches;
                total_dl += chain_mgr.GetBlock(h).GetRoughSize();
                // Obtain transactions
                const auto txs = chain_mgr.GetBlock(h).GetTxsByScript(script_idx);
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
        Script script = chain_mgr.GetScriptPool().GetScript(script_idx);
        const auto& filters = this->block_range_filter_sets.find(block_range_size)->second;
        size_t block_range_filter_matches{0};
        size_t negative_block_range_filter_matches{0};
        size_t block_filter_matches{0};
        size_t negative_block_filter_matches{0};
        size_t total_filters{0};
        size_t total_txs{0};
        size_t total_dl{0};
        // Check each block RANGE filter
        for (uint32_t h = SKIP_BLOCKS; h < chain_mgr.size(); h += block_range_size) {
            const auto& block_range_filter = filters[h / block_range_size];
            total_dl += block_range_filter.GetEncoded().size();
            // printf("h %d \n", h);
            if (block_range_filter.Match(script)) {
                // range filter matches, we download & check each block filter
                // printf("Range block filter height = %d - %d (%d) matched! \n", h, h + block_range_size - 1, h / block_range_size);
                ++block_range_filter_matches;
                size_t filters2{0};
                size_t txs2{0};
                for (uint32_t h2 = h; h2 < h + block_range_size && h2 < chain_mgr.size(); ++h2) {
                    const auto& block_filter = this->block_filters[h2];
                    total_dl += block_filter.GetEncoded().size();
                    // printf("h2 %d \n", h2);
                    if (block_filter.Match(script)) {
                        // filter matches, we download & check the block
                        // printf("Block filter height = %d matched!", h);
                        ++block_filter_matches;
                        total_dl += chain_mgr.GetBlock(h2).GetRoughSize();
                        // Obtain transactions
                        const auto txs = chain_mgr.GetBlock(h2).GetTxsByScript(script_idx);
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
                    total_dl += chain_mgr.GetBlock(h).GetRoughSize();
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
}; // FilterManager

Transaction::Transaction(const Blockchain& chain, const ScriptPool& scriptpool, std::vector<Input> inputs, std::vector<Output> outputs) {
    this->inputs.clear();
    for (const auto& inp : inputs) {
        if (!chain.HasInput(inp)) {
            printf("Tx::Tx HasInput failed! "); inp.print(); printf(" \n");
            assert(false);
        }
        this->inputs.emplace_back(inp);
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

Block::Block(const Blockchain& chain, uint64_t height, std::vector<Transaction> transactions, FastRandomContext& rng) {
    this->height = height;
    SetRandomBlockHash(rng);

    txs = transactions;
    this->total_rough_size = 0;

    this->scripts.clear();
    this->scripts.reserve(DEFAULT_SCRIPTS_PER_BLOCK);
    for (size_t ti = 0; ti < this->txs.size(); ++ti) {
        const auto& tx = this->txs[ti];
        for (const auto& inp : tx.GetInputs()) {
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
        scripts3.emplace_back(manager.PickScriptIndexWithDesiredBlockOccurance(3, 6));
    }

    for (const auto& script: scripts3) {
        auto result1 = filter_mgr.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts3) {
            auto result = filter_mgr.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    std::vector<ScriptIdx> scripts20;
    for (auto i = 0; i < 6; ++i) {
        scripts20.emplace_back(manager.PickScriptIndexWithDesiredBlockOccurance(20, 30));
    }

    for (const auto& script: scripts20) {
        auto result1 = filter_mgr.RunBlockFilterSimulation(script);
    }
    for (auto block_range_size: block_range_sizes) {
        for (const auto& script: scripts20) {
            auto result = filter_mgr.RunBlockRangeSimulation(block_range_size, script);
        }
    }

    // printf("scriptpool %ld\n", scriptpool.size());
    // for (auto i = 0; i < 20; ++i) {
    //     printf("  %ld\n", scriptpool.PickIndexWithSkewedProb(m_rng));
    // }
}

BOOST_AUTO_TEST_SUITE_END()

