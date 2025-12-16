#ifndef SCHED_TX_H
#define SCHED_TX_H

//#define __SCHED_TX_STANDALONE_PROTO__

#ifdef __SCHED_TX_STANDALONE_PROTO__
// Stub for standalone proto sched-tx-proto
#include "bitcoincore.h"
#else
#include <init.h>
#include <node/types.h>
#include <primitives/transaction_identifier.h>
#endif

#include <any>
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
    std::uint64_t max_fee;
    node::TxBroadcast broadcast_method;
    // The serialized transaction
    std::vector<uint8_t> tx;

    // Constructor with parameters
    ScheduledTx(std::uint64_t submitted_time,
        std::uint64_t target_time,
        const std::vector<std::uint8_t>& tx,
        uint64_t max_fee = 1000000,
        node::TxBroadcast broadcast_method = node::TxBroadcast::MEMPOOL_AND_BROADCAST_TO_ALL
    )
        : submitted_time(submitted_time)
        , target_time(target_time)
        , max_fee(max_fee)
        , broadcast_method(broadcast_method)
        , tx(std::move(tx)) {}

    // Return the scheduled target time, or next retry time
    uint32_t GetTargetTime() const;

    // Accessor for the size of the transaction
    uint32_t size() const {
        return static_cast<uint32_t>(this->tx.size());
    }

    std::string ToString() const;
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
    std::optional<ScheduledTx> GetOneProcessable(uint32_t current_time);
};

class ScheduledTxPool {
private:
    std::any node_context;
    ScheduledTxCollection pool;
    std::string file_name;
    std::mutex mtx;
    std::thread worker;
    bool running;

public:
    ScheduledTxPool(std::any& node_context);

    ~ScheduledTxPool() {
        Stop();
    }

    void Start();

    void Stop();

    /// Schedule a new transaction
    /// Can throw if already at maximum size
    Txid ScheduleTx(uint32_t target_time,
        const std::vector<uint8_t>& tx,
        uint64_t max_fee = 1000000,
        node::TxBroadcast broadcast_method = node::TxBroadcast::MEMPOOL_AND_BROADCAST_TO_ALL);

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
};

#endif // SCHED_TX_H
