#include "schedtx.h"

#ifndef __SCHED_TX_STANDALONE_PROTO__
#include <node/context.h>
#include <node/transaction.h>
#include <primitives/transaction.h>
#include <crypto/hex_base.h>
#include <core_io.h>
#include <rpc/server_util.h>
#include <validation.h>
#endif

#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <vector>
#include <sstream>
#include <thread>
#include <tuple>


// Return the scheduled target time
uint32_t ScheduledTx::GetTargetTime() const {
    return this->target_time;
}

std::string ScheduledTx::ToString() const {
    std::stringstream ss;
    ss << this->submitted_time << " " << this->target_time << " " << this->size();
    return ss.str();
}

void ScheduledTxCollection::AddInternal(ScheduledTx& tx) {
    // TODO lock
    if (this->tx.size() >= MAX_SCHEDULED_TX_COUNT) {
        throw std::invalid_argument("Maximum scheduled transaction count reached");
    }
    this->tx.emplace_back(tx);
    assert(this->tx.size() <= MAX_SCHEDULED_TX_COUNT);
}

void ScheduledTxCollection::RemoveInternal(size_t index) {
    // TOOD lock
    if (index > this->tx.size()) {
        return;
    }
    this->tx.erase(this->tx.begin() + index);
}

// Get the earliest target time and the index of the transaction with this time.
// If there are no transactions nullopt is returned
std::optional<std::tuple<uint32_t, size_t>> ScheduledTxCollection::GetEarliest() const {
    if (this->tx.size() == 0) {
        return std::nullopt;
    }
    // TODO simpler impl with max
    size_t index(0);
    assert(this->tx.size() > index);
    uint32_t time{this->tx[index].target_time};
    for (size_t i{1}; i < this->tx.size(); ++i) {
        if (this->tx[i].target_time < time) {
            index = i;
            time = this->tx[index].target_time;
        }
    }
    return std::optional<std::tuple<uint32_t, size_t>>({time, index});
}

std::optional<ScheduledTx> ScheduledTxCollection::GetOneProcessable(uint32_t current_time) {
    for (size_t i{0}; i < this->tx.size(); ++i) {
        // Check by absolute time
        if (this->tx[i].GetTargetTime() <= current_time) {
            auto to_process = this->tx[i];
            // Do not save now, not to loose while processing
            this->RemoveInternal(i);
            return to_process;
        }
    }
    return std::nullopt;
}

std::string ScheduledTxCollection::ToString() const {
    std::stringstream ss;
    if (this->tx.empty()) {
        ss << "Pool with no txs";
    } else {
        ss << "Pool with " << this->tx.size() << " txs, next_time " << std::get<0>(this->GetEarliest().value());
    }
    return ss.str();
}

CTransaction ParseTransaction(const std::vector<uint8_t>& data) {
    #ifndef __SCHED_TX_STANDALONE_PROTO__
    // A bit of workaround: first make it into hex string
    std::string hexstr = HexStr(data);
    CMutableTransaction mtx;
    assert(DecodeHexTx(mtx, hexstr));
    // MakeTransactionRef(std::move(mtx));
    CTransaction ctx(mtx);
    return ctx;
    #else
    return CTransaction(data);
    #endif
}


ScheduledTxPool::ScheduledTxPool(std::any& node_context) : node_context(std::move(node_context)), running(false) {
    assert(this->node_context.has_value());
    // auto& typed_node_context = EnsureAnyNodeContext(this->node_context);
}

void ScheduledTxPool::Start() {
    this->running = true;
    this->worker = std::thread([this]() {
        this->ProcessInLoop();
    });
}

void ScheduledTxPool::Stop() {
    bool was_running = false;
    {
        std::lock_guard<std::mutex> lock(this->mtx);
        was_running = this->running;
        if (this->running) {
            this->running = false;
        }
    }
    if (was_running) {
        if (this->worker.joinable()) {
            this->worker.join();
        }
    }
}

Txid ScheduledTxPool::ScheduleTx(uint32_t target_time, const std::vector<uint8_t>& tx, uint64_t max_fee, node::TxBroadcast broadcast_method) {
    auto now = time(NULL);
    // Parse TX, obtain Txid
    CTransaction ctx = ParseTransaction(tx);
    Txid txid = ctx.GetHash();
    ScheduledTx stx(now, target_time, tx, max_fee, broadcast_method);
    {
        std::lock_guard<std::mutex> lock(this->mtx);
        // may throw
        this->pool.AddInternal(stx);
    }
    return txid;
}

/// Check transactions, and if one ready for processing is found, process it
/// @return true if a tx was processed
bool ScheduledTxPool::ProcessOne(uint32_t current_time_override) {
    uint32_t current_time{current_time_override};
    if (current_time == 0) {
        current_time = time(NULL);
    }

    std::optional<ScheduledTx> to_process{std::nullopt};
    {
        std::lock_guard<std::mutex> lock(this->mtx);
        to_process = this->pool.GetOneProcessable(current_time);
    }
    if (!to_process.has_value()) {
        return false;
    }

    assert(to_process.has_value());
    if (!this->ProcessTx(to_process.value(), current_time)) {
        // failure, put it back
        {
            std::lock_guard<std::mutex> lock(this->mtx);
            this->pool.AddInternal(to_process.value());
        }
        return true;
    } else {
        // Processing was OK
        return true;
    }
}

/// Process transactions in a loop.
// TODO Should be done in a background loop
// TODO Proper exit
void ScheduledTxPool::ProcessInLoop(uint32_t current_time_override) {
    auto current_time = time(NULL);
    uint32_t time_delta = 0;
    if (current_time_override) {
        time_delta = current_time_override - current_time;
    }

    printf("ScheduledTxPool starting, count=%ld\n", this->Count());
    {
        std::lock_guard<std::mutex> lock(this->mtx);
        auto earliest = this->pool.GetEarliest();
        if (earliest.has_value()) {
            auto early = std::get<0>(earliest.value());
            printf("Earliest next time: in %ld sec, %d\n", early - current_time - time_delta, early);
        }
    }

    while(this->running) {
        current_time = time(NULL);
        if (this->ProcessOne(current_time + time_delta)) {
            // printf("%ld: '%s'\n", current_time + time_delta, this->ToString().c_str());
            {
                std::lock_guard<std::mutex> lock(this->mtx);
                current_time = time(NULL);
                auto earliest = this->pool.GetEarliest();
                if (earliest.has_value()) {
                    auto early = std::get<0>(earliest.value());
                    printf("Earliest next time: in %ld sec, %d\n", early - current_time - time_delta, early);
                }
            }
            continue;
        }

        if (!this->running) {
            break;
        }

        // No Tx for processing for now
        // Sleep for a small time
        // TODO: wake up on running change
        auto to_wait = 3;
        auto earliest_opt = this->pool.GetEarliest();
        if (earliest_opt.has_value()) {
            auto earliest = std::get<0>(earliest_opt.value());
            current_time = time(NULL);
            auto earliest_delta = earliest - current_time;
            if (earliest_delta < to_wait) {
                if (earliest_delta < 0) {
                    to_wait = 0;
                } else {
                    to_wait = earliest_delta;
                }
            }
        }
        // printf("to_wait: %d\n", to_wait);
        if (to_wait > 0) {
            std::this_thread::sleep_for(std::chrono::seconds(to_wait));
        }

        current_time = time(NULL);
    }
}

/// Process a transaction now
/// @return true if the tx was processed
bool ScheduledTxPool::ProcessTx(const ScheduledTx& tx, uint32_t current_time) {
    printf("Broadcasting tx (size %d '%s'), now %d ... \n", tx.size(), tx.ToString().c_str(), current_time);
    assert(this->node_context.has_value());
    auto& typed_node_context = EnsureAnyNodeContext(this->node_context);
    CTransaction ctx = ParseTransaction(tx.tx);
    std::string broadcast_error;
    // TODO Check if wait_callback=true needed?
    auto res = BroadcastTransaction(typed_node_context, std::make_shared<const CTransaction>(ctx), broadcast_error, static_cast<CAmount>(tx.max_fee), tx.broadcast_method, /*wat_callback=*/true);
    printf("Broadcast result: %d, now %d, txid %s\n", int(res), current_time, ctx.GetHash().ToString().c_str());
    return true;
}

