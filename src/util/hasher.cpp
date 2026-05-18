// Copyright (c) 2019-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <util/hasher.h>

#include <crypto/siphash.h>
#include <random.h>

SaltedUint256Hasher::SaltedUint256Hasher() : m_hasher{
    FastRandomContext().rand64(),
    FastRandomContext().rand64()}
{}

SaltedTxidHasher::SaltedTxidHasher() : m_hasher{
    FastRandomContext().rand64(),
    FastRandomContext().rand64()}
{}

SaltedWtxidHasher::SaltedWtxidHasher() : m_hasher{
    FastRandomContext().rand64(),
    FastRandomContext().rand64()}
{}

uint64_t PresaltedXorHasher6x8_1::operator()(const uint256& val, uint32_t extra) const noexcept
{
    return
        m_salt ^
        val.GetUint64(0) ^
        val.GetUint64(1) ^
        val.GetUint64(2) ^
        val.GetUint64(3) ^
        uint64_t(extra);
}

uint64_t PresaltedXorHasher6x8_2::operator()(const uint256& val, uint32_t extra) const noexcept
{
    return
        (val.GetUint64(0) ^ m_salt0) +
        (val.GetUint64(1) ^ m_salt1) +
        (val.GetUint64(2) ^ m_salt2) +
        (val.GetUint64(3) ^ m_salt3) +
        uint64_t(extra);
}

uint64_t DummyHasherFirst8Plus::operator()(const uint256& val, uint32_t extra) const noexcept
{
    return val.GetUint64(0) + uint64_t(extra);
}

SaltedOutpointHasher::SaltedOutpointHasher(bool deterministic) : m_hasher{
    deterministic ? 0x8e819f2607a18de6 : FastRandomContext().rand64(),
    deterministic ? 0xf4020d2e3983b0eb : FastRandomContext().rand64(),
    deterministic ? 0x1e6f3460eb3e0f04 : FastRandomContext().rand64(),
    deterministic ? 0x82354bb5318e6169 : FastRandomContext().rand64()}
{}

SaltedSipHasher::SaltedSipHasher() :
    m_k0{FastRandomContext().rand64()},
    m_k1{FastRandomContext().rand64()}
{}

size_t SaltedSipHasher::operator()(const std::span<const unsigned char>& script) const
{
    return CSipHasher(m_k0, m_k1).Write(script).Finalize();
}
