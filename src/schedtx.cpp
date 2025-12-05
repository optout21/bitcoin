#include "schedtx.h"

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


// Return the scheduled target time, or next retry time
uint32_t ScheduledTx::GetTargetTime() const {
    return this->target_time + this->retry_count * this->retry_period;
}

std::string ScheduledTx::ToString() const {
    std::stringstream ss;
    ss << this->submitted_time << " " << this->target_time << " " << this->size() << " (" << (int)this->max_retries << " x " << this->retry_period << ")";
    return ss.str();
}

// Serialize the ScheduledTx to a stream
void ScheduledTx::Serialize(std::ostream& stream) const {
    // Write fields in order with fixed size
    stream.write(reinterpret_cast<const char*>(&submitted_time), sizeof(submitted_time));
    stream.write(reinterpret_cast<const char*>(&target_time), sizeof(target_time));
    stream.write(reinterpret_cast<const char*>(&max_retries), sizeof(max_retries));
    stream.write(reinterpret_cast<const char*>(&retry_period), sizeof(retry_period));
    stream.write(reinterpret_cast<const char*>(&retry_count), sizeof(retry_count));
    stream.write(reinterpret_cast<const char*>(&last_try_time), sizeof(last_try_time));

    // Write tx size first, then the tx data
    uint32_t tx_size = static_cast<uint32_t>(tx.size());
    stream.write(reinterpret_cast<const char*>(&tx_size), sizeof(tx_size));
    if (tx_size > 0) {
        stream.write(reinterpret_cast<const char*>(tx.data().data()), tx_size);
    }
}

// Deserialize from a stream and construct an object
ScheduledTx ScheduledTx::Deserialize(std::istream& stream) {
    ScheduledTx obj;

    // TODO stream size check, error handling

    // Read fields in order
    stream.read(reinterpret_cast<char*>(&obj.submitted_time), sizeof(obj.submitted_time));
    stream.read(reinterpret_cast<char*>(&obj.target_time), sizeof(obj.target_time));
    stream.read(reinterpret_cast<char*>(&obj.max_retries), sizeof(obj.max_retries));
    stream.read(reinterpret_cast<char*>(&obj.retry_period), sizeof(obj.retry_period));
    stream.read(reinterpret_cast<char*>(&obj.retry_count), sizeof(obj.retry_count));
    stream.read(reinterpret_cast<char*>(&obj.last_try_time), sizeof(obj.last_try_time));

    // Read tx size first, then the tx data
    uint32_t tx_size;
    stream.read(reinterpret_cast<char*>(&tx_size), sizeof(tx_size));
    std::vector<uint8_t> txdata;
    if (tx_size > 0) {
        txdata.resize(tx_size);
        stream.read(reinterpret_cast<char*>(txdata.data()), tx_size);
        obj.tx = CTransaction(txdata);
    }

    return obj;
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
    for (auto i{1}; i < this->tx.size(); ++i) {
        if (this->tx[i].target_time < time) {
            index = i;
            time = this->tx[index].target_time;
        }
    }
    return std::optional<std::tuple<uint32_t, size_t>>({time, index});
}

std::optional<ScheduledTx> ScheduledTxCollection::GetOneProcessable(int32_t current_time) {
    for (auto i{0}; i < this->tx.size(); ++i) {
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

// Serialize the transactions to a stream
uint32_t ScheduledTxCollection::Serialize(std::ostream& stream) const {
    uint32_t count = static_cast<uint32_t>(this->tx.size());
    stream.write(reinterpret_cast<const char*>(&count), sizeof(count));
    // TODO lock
    for (size_t i{0}; i < this->tx.size(); ++i) {
        this->tx[i].Serialize(stream);
    }
    {
        // Add human-readable text for debug
        std::string text = this->ToString();
        stream.write(reinterpret_cast<const char*>(text.c_str()), text.size());
    }
    return static_cast<uint32_t>(this->tx.size());
}

// Deerialize the transactions from a stream
ScheduledTxCollection ScheduledTxCollection::Deserialize(std::istream& stream) {
    ScheduledTxCollection pool;

    // TODO stream size check, error handling

    uint32_t count;
    stream.read(reinterpret_cast<char*>(&count), sizeof(count));
    for (auto i{0}; i < count; ++i) {
        auto tx1 = ScheduledTx::Deserialize(stream);
        pool.tx.emplace_back(tx1);
    }

    return pool;
}


void ScheduledTxPool::CreateFromFile(const char* filename) {
    this->file_name = std::string(filename);
    auto read = ReadFromFile(this->file_name);
    if (read.has_value()) {
        std::lock_guard<std::mutex> lock(this->mtx);
        this->pool = read.value();
    }
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

Txid ScheduledTxPool::Add(uint32_t target_time, const ByteArray& tx, std::uint8_t max_retries, std::uint32_t retry_period) {
    auto now = time(NULL);
    CTransaction ctx(tx);
    Txid txid = ctx.GetHash();
    ScheduledTx stx(now, target_time, tx, max_retries, retry_period);
    {
        std::lock_guard<std::mutex> lock(this->mtx);
        // may throw
        this->pool.AddInternal(stx);
    }
    this-SaveIfNeeded();
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
        this->SaveIfNeeded();
        return true;
    } else {
        // Processing was OK, save it now
        this->SaveIfNeeded();
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

    printf("ScheduledTxCollection starting, count=%ld\n", this->Count());
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
    auto chainman = this->node_context.EnsureChainman();
    auto res = chainman.ProcessTransaction(std::make_shared<const CTransaction>(tx.tx));
    printf("Broadcast result: %d, now %d\n", int(res.m_result_type), current_time);
    return true;
}

// Write the pool to a file
uint32_t ScheduledTxPool::WriteToFile(const std::string& filename) {
    std::ofstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return -1;
    }

    std::lock_guard<std::mutex> lock(this->mtx);
    auto res = this->pool.Serialize(file);
    file.close();
    return res;
}

// Static method to read a ScheduledTxCollection object from a file
std::optional<ScheduledTxCollection> ScheduledTxPool::ReadFromFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
        return std::nullopt;
    }
    
    try {
        ScheduledTxCollection pool = ScheduledTxCollection::Deserialize(file);
        file.close();
        return pool;
    } catch (...) {
        file.close();
        return std::nullopt;
    }
}

uint32_t ScheduledTxPool::SaveIfNeeded() {
    if (this->file_name.length() > 0) {
        auto count = this->WriteToFile(this->file_name);
        if (count >= 0) {
            printf("Written to file, count %d, file '%s'\n", count, this->file_name.c_str());
        }
        return count;
    } else {
        return false;
    }
}

