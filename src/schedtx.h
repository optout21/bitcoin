#pragma once

#include "bitcoincore.h"

#include <cstdint>
#include <iostream>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>
#include <tuple>


class ScheduledTx {
public:
    // Time of submission, unix time (utc)
    std::uint32_t submitted_time;
    // Target time for broadcasting, absolute time or block height
    // Semantics is like in lock_time
    std::uint32_t target_time;
    std::uint8_t max_retries;
    std::uint32_t retry_period;
    std::uint8_t retry_count;
    std::uint32_t last_try_time;
    // The serialized transaction
    // TODO Store CTransaction?
    CTransaction tx;

    // Default constructor
    ScheduledTx() 
        : submitted_time(0)
        , target_time(0)
        , max_retries(1)
        , retry_period(3600)
        , retry_count(0)
        , last_try_time(0)
        , tx() {}

    // Constructor with parameters
    ScheduledTx(std::uint64_t submitted_time, std::uint64_t target_time, const std::vector<std::uint8_t>& tx, std::uint8_t max_retries = 1, std::uint32_t retry_period = 3600)
        : submitted_time(submitted_time)
        , target_time(target_time)
        , max_retries(max_retries)
        , retry_period(retry_period)
        , retry_count(0)
        , last_try_time(0)
        , tx(std::move(tx)) {}

    // Return the scheduled target time, or next retry time
    uint32_t GetTargetTime() const;

    // Accessor for the size of the transaction
    uint32_t size() const {
        return static_cast<uint32_t>(this->tx.size());
    }

    std::string ToString() const;

    // Serialize the ScheduledTx to a stream
    void Serialize(std::ostream& stream) const;

    // Deserialize from a stream and construct an object
    static ScheduledTx Deserialize(std::istream& stream);
};

class ScheduledTxCollection {
protected:
    std::vector<ScheduledTx> tx;

public:
    static const uint32_t MAX_SCHEDULED_TX_COUNT = 1000;

    ScheduledTxCollection() {}
    // ScheduledTxCollection(const std::vector<ScheduledTx>& txs) : tx(std::move(txs)) {}

    size_t Count() const { return this->tx.size(); }

    std::string ToString() const;

    /// Internal use. Can throw if already at maximum size
    void AddInternal(ScheduledTx& tx);

    void RemoveInternal(size_t index);

    // Get the earliest target time and the index of the transaction with this time.
    // If there are no transactions nullopt is returned
    std::optional<std::tuple<uint32_t, size_t>> GetEarliest() const;

    /// Return one one ready for processing, if found. Also removes it.
    std::optional<ScheduledTx> GetOneProcessable(int32_t current_time);

    // Serialize the transactions to a stream
    uint32_t Serialize(std::ostream& stream) const;

    // Deerialize the transactions from a stream
    static ScheduledTxCollection Deserialize(std::istream& stream);
};

class ScheduledTxPool {
private:
    NodeContext node_context;
    ScheduledTxCollection pool;
    std::string file_name;
    std::mutex mtx;
    std::thread worker;
    bool running;

public:
    ScheduledTxPool(NodeContext& node_context) : node_context(node_context), running(false) {}

    ~ScheduledTxPool() {
        Stop();
    }

    void CreateFromFile(const char* filename);

    void Start();

    void Stop();

    /// Schedule a new transaction
    /// Can throw if already at maximum size
    Txid Add(uint32_t target_time, const ByteArray& tx, std::uint8_t max_retries = 1, std::uint32_t retry_period = 3600);

    size_t Count() const { return this->pool.Count(); }

    std::string ToString() const { return this->pool.ToString(); }

protected:
    /// Check transactions, and if one ready for processing is found, process it
    /// @return true if a tx was processed
    bool ProcessOne(uint32_t current_time_override);

    /// Process transactions in a loop.
    void ProcessInLoop(uint32_t current_time_override = 0);

    /// Process a transaction now
    /// @return true if the tx was processed
    bool ProcessTx(const ScheduledTx& tx, uint32_t current_time);

    // Write the pool to a file
    uint32_t WriteToFile(const std::string& filename);

    // Static method to read a ScheduledTxCollection object from a file
    static std::optional<ScheduledTxCollection> ReadFromFile(const std::string& filename);

    uint32_t SaveIfNeeded();
};
