/*
    Copyright 2022 The Silkrpc Authors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "backend_grpc.hpp"

#include <optional>
#include <vector>

#include <boost/endian/conversion.hpp>
#include <grpcpp/grpcpp.h>

#include <silkrpc/common/clock_time.hpp>
#include <silkrpc/common/log.hpp>
#include <silkrpc/config.hpp>

namespace silkrpc::ethbackend {

asio::awaitable<evmc::address> BackEndGrpc::etherbase() {
        const auto start_time = clock_time::now();
        EtherbaseAwaitable eb_awaitable{executor_, stub_, queue_};
        const auto reply = co_await eb_awaitable.async_call(::remote::EtherbaseRequest{}, asio::use_awaitable);
        evmc::address evmc_address;
        if (reply.has_address()) {
            const auto h160_address = reply.address();
            evmc_address = address_from_H160(h160_address);
        }
        SILKRPC_DEBUG << "BackEnd::etherbase address=" << evmc_address << " t=" << clock_time::since(start_time) << "\n";
        co_return evmc_address;
}

asio::awaitable<uint64_t> BackEndGrpc::protocol_version() {
    const auto start_time = clock_time::now();
    ProtocolVersionAwaitable pv_awaitable{executor_, stub_, queue_};
    const auto reply = co_await pv_awaitable.async_call(::remote::ProtocolVersionRequest{}, asio::use_awaitable);
    const auto pv = reply.id();
    SILKRPC_DEBUG << "BackEnd::protocol_version version=" << pv << " t=" << clock_time::since(start_time) << "\n";
    co_return pv;
}

asio::awaitable<uint64_t> BackEndGrpc::net_version() {
    const auto start_time = clock_time::now();
    NetVersionAwaitable nv_awaitable{executor_, stub_, queue_};
    const auto reply = co_await nv_awaitable.async_call(::remote::NetVersionRequest{}, asio::use_awaitable);
    const auto nv = reply.id();
    SILKRPC_DEBUG << "BackEnd::net_version version=" << nv << " t=" << clock_time::since(start_time) << "\n";
    co_return nv;
}

asio::awaitable<std::string> BackEndGrpc::client_version() {
    const auto start_time = clock_time::now();
    ClientVersionAwaitable cv_awaitable{executor_, stub_, queue_};
    const auto reply = co_await cv_awaitable.async_call(::remote::ClientVersionRequest{}, asio::use_awaitable);
    const auto cv = reply.nodename();
    SILKRPC_DEBUG << "BackEnd::client_version version=" << cv << " t=" << clock_time::since(start_time) << "\n";
    co_return cv;
}

asio::awaitable<uint64_t> BackEndGrpc::net_peer_count() {
    const auto start_time = clock_time::now();
    NetPeerCountAwaitable npc_awaitable{executor_, stub_, queue_};
    const auto reply = co_await npc_awaitable.async_call(::remote::NetPeerCountRequest{}, asio::use_awaitable);
    const auto count = reply.count();
    SILKRPC_DEBUG << "BackEnd::net_peer_count count=" << count << " t=" << clock_time::since(start_time) << "\n";
    co_return count;
}

asio::awaitable<ExecutionPayload> BackEndGrpc::engine_get_payload_v1(uint64_t payload_id) {
    const auto start_time = clock_time::now();
    EngineGetPayloadV1Awaitable npc_awaitable{executor_, stub_, queue_};
    ::remote::EngineGetPayloadRequest req;
    req.set_payloadid(payload_id);
    const auto reply = co_await npc_awaitable.async_call(req, asio::use_awaitable);
    auto execution_payload{decode_execution_payload(reply)};
    SILKRPC_DEBUG << "BackEnd::engine_get_payload_v1 data=" << execution_payload << " t=" << clock_time::since(start_time) << "\n";
    co_return execution_payload;
}

asio::awaitable<PayloadStatus> BackEndGrpc::engine_new_payload_v1(ExecutionPayload payload) {
    const auto start_time = clock_time::now();
    EngineNewPayloadV1Awaitable npc_awaitable{executor_, stub_, queue_};
    auto req{encode_execution_payload(payload)};
    const auto reply = co_await npc_awaitable.async_call(req, asio::use_awaitable);
    PayloadStatus payload_status;
    payload_status.status = decode_status_message(reply.status());
    // Set LatestValidHash (if there is one)
    if (reply.has_latestvalidhash()) {
        payload_status.latest_valid_hash = bytes32_from_H256(reply.latestvalidhash());
    }
    // Set ValidationError (if there is one)
    const auto validation_error{reply.validationerror()};
    if (validation_error != "") {
        payload_status.validation_error = validation_error;
    }
    SILKRPC_DEBUG << "BackEnd::engine_new_payload_v1 data=" << payload_status << " t=" << clock_time::since(start_time) << "\n";
    co_return payload_status;
}

evmc::address BackEndGrpc::address_from_H160(const types::H160& h160) {
    uint64_t hi_hi = h160.hi().hi();
    uint64_t hi_lo = h160.hi().lo();
    uint32_t lo = h160.lo();
    evmc::address address{};
    boost::endian::store_big_u64(address.bytes +  0, hi_hi);
    boost::endian::store_big_u64(address.bytes +  8, hi_lo);
    boost::endian::store_big_u32(address.bytes + 16, lo);
    return address;
}

silkworm::Bytes BackEndGrpc::bytes_from_H128(const types::H128& h128) {
    silkworm::Bytes bytes(16, '\0');
    boost::endian::store_big_u64(&bytes[0], h128.hi());
    boost::endian::store_big_u64(&bytes[8], h128.lo());
    return bytes;
}

types::H128* BackEndGrpc::H128_from_bytes(const uint8_t* bytes) {
    auto h128{new types::H128()};
    h128->set_hi(boost::endian::load_big_u64(bytes));
    h128->set_lo(boost::endian::load_big_u64(bytes + 8));
    return h128;
}

types::H160* BackEndGrpc::H160_from_address(const evmc::address& address) {
    auto h160{new types::H160()};
    auto hi{H128_from_bytes(address.bytes)};
    h160->set_allocated_hi(hi);
    h160->set_lo(boost::endian::load_big_u32(address.bytes + 16));
    return h160;
}

types::H256* BackEndGrpc::H256_from_bytes(const uint8_t* bytes) {
    auto h256{new types::H256()};
    auto hi{H128_from_bytes(bytes)};
    auto lo{H128_from_bytes(bytes + 16)};
    h256->set_allocated_hi(hi);
    h256->set_allocated_lo(lo);
    return h256;
}

silkworm::Bytes BackEndGrpc::bytes_from_H256(const types::H256& h256) {
    silkworm::Bytes bytes(32, '\0');
    auto hi{h256.hi()};
    auto lo{h256.lo()};
    std::memcpy(&bytes[0], bytes_from_H128(hi).data(), 16);
    std::memcpy(&bytes[16], bytes_from_H128(lo).data(), 16);
    return bytes;
}

intx::uint256 BackEndGrpc::uint256_from_H256(const types::H256& h256) {
    intx::uint256 n;
    n[3] = h256.hi().hi();
    n[2] = h256.hi().lo();
    n[1] = h256.lo().hi();
    n[0] = h256.lo().lo();
    return n;
}

types::H256* BackEndGrpc::H256_from_uint256(const intx::uint256& n) {
    auto h256{new types::H256()};
    auto hi{new types::H128()};
    auto lo{new types::H128()};

    hi->set_hi(n[3]);
    hi->set_lo(n[2]);
    lo->set_hi(n[1]);
    lo->set_lo(n[0]);

    h256->set_allocated_hi(hi);
    h256->set_allocated_lo(lo);
    return h256;
}

evmc::bytes32 BackEndGrpc::bytes32_from_H256(const types::H256& h256) {
    evmc::bytes32 bytes32;
    std::memcpy(bytes32.bytes, bytes_from_H256(h256).data(), 32);
    return bytes32;
}

types::H512* BackEndGrpc::H512_from_bytes(const uint8_t* bytes) {
    auto h512{new types::H512()};
    auto hi{H256_from_bytes(bytes)};
    auto lo{H256_from_bytes(bytes + 32)};
    h512->set_allocated_hi(hi);
    h512->set_allocated_lo(lo);
    return h512;
}

silkworm::Bytes BackEndGrpc::bytes_from_H512(types::H512& h512) {
    silkworm::Bytes bytes(64, '\0');
    auto hi{h512.hi()};
    auto lo{h512.lo()};
    std::memcpy(&bytes[0], bytes_from_H256(hi).data(), 32);
    std::memcpy(&bytes[32], bytes_from_H256(lo).data(), 32);
    return bytes;
}

types::H1024* BackEndGrpc::H1024_from_bytes(const uint8_t* bytes) {
    auto h1024{new types::H1024()};
    auto hi{H512_from_bytes(bytes)};
    auto lo{H512_from_bytes(bytes + 64)};
    h1024->set_allocated_hi(hi);
    h1024->set_allocated_lo(lo);
    return h1024;
}

silkworm::Bytes BackEndGrpc::bytes_from_H1024(types::H1024& h1024) {
    silkworm::Bytes bytes(128, '\0');
    auto hi{h1024.hi()};
    auto lo{h1024.lo()};
    std::memcpy(&bytes[0], bytes_from_H512(hi).data(), 64);
    std::memcpy(&bytes[64], bytes_from_H512(lo).data(), 64);
    return bytes;
}

types::H2048* BackEndGrpc::H2048_from_bytes(const uint8_t* bytes) {
    auto h2048{new types::H2048()};
    auto hi{H1024_from_bytes(bytes)};
    auto lo{H1024_from_bytes(bytes + 128)};
    h2048->set_allocated_hi(hi);
    h2048->set_allocated_lo(lo);
    return h2048;
}

silkworm::Bytes BackEndGrpc::bytes_from_H2048(types::H2048& h2048) {
    silkworm::Bytes bytes(256, '\0');
    auto hi{h2048.hi()};
    auto lo{h2048.lo()};
    std::memcpy(&bytes[0], bytes_from_H1024(hi).data(), 128);
    std::memcpy(&bytes[128], bytes_from_H1024(lo).data(), 128);
    return bytes;
}

ExecutionPayload BackEndGrpc::decode_execution_payload(const types::ExecutionPayload& execution_payload_grpc) {
    auto state_root_h256{execution_payload_grpc.stateroot()};
    auto receipts_root_h256{execution_payload_grpc.receiptroot()};
    auto block_hash_h256{execution_payload_grpc.blockhash()};
    auto parent_hash_h256{execution_payload_grpc.parenthash()};
    auto prev_randao_h256{execution_payload_grpc.prevrandao()};
    auto base_fee_h256{execution_payload_grpc.basefeepergas()};
    auto logs_bloom_h2048{execution_payload_grpc.logsbloom()};
    auto extra_data_string{execution_payload_grpc.extradata()}; // []byte becomes std::string in silkrpc protobuf
    // Convert h2048 to a bloom
    silkworm::Bloom bloom;
    std::memcpy(&bloom[0], bytes_from_H2048(logs_bloom_h2048).data(), 256);
    // Convert transactions in std::string to silkworm::Bytes
    std::vector<silkworm::Bytes> transactions;
    for (const auto& transaction_string : execution_payload_grpc.transactions()) {
        transactions.push_back(silkworm::bytes_of_string(transaction_string));
    }

    // Assembling the execution_payload data structure
    return ExecutionPayload{
        .number = execution_payload_grpc.blocknumber(),
        .timestamp = execution_payload_grpc.timestamp(),
        .gas_limit = execution_payload_grpc.gaslimit(),
        .gas_used = execution_payload_grpc.gasused(),
        .suggested_fee_recipient = address_from_H160(execution_payload_grpc.coinbase()),
        .state_root = bytes32_from_H256(state_root_h256),
        .receipts_root = bytes32_from_H256(receipts_root_h256),
        .parent_hash = bytes32_from_H256(parent_hash_h256),
        .block_hash = bytes32_from_H256(block_hash_h256),
        .prev_randao = bytes32_from_H256(prev_randao_h256),
        .base_fee = uint256_from_H256(base_fee_h256),
        .logs_bloom = bloom,
        .extra_data = silkworm::bytes_of_string(extra_data_string),
        .transactions = transactions
    };
}

types::ExecutionPayload BackEndGrpc::encode_execution_payload(const ExecutionPayload& execution_payload) {
    types::ExecutionPayload execution_payload_grpc;
    // Numerical parameters
    execution_payload_grpc.set_blocknumber(execution_payload.number);
    execution_payload_grpc.set_timestamp(execution_payload.timestamp);
    execution_payload_grpc.set_gaslimit(execution_payload.gas_limit);
    execution_payload_grpc.set_gasused(execution_payload.gas_used);
    // coinbase
    execution_payload_grpc.set_allocated_coinbase(H160_from_address(execution_payload.suggested_fee_recipient));
    // 32-bytes parameters
    execution_payload_grpc.set_allocated_receiptroot(H256_from_bytes(execution_payload.receipts_root.bytes));
    execution_payload_grpc.set_allocated_stateroot(H256_from_bytes(execution_payload.state_root.bytes));
    execution_payload_grpc.set_allocated_parenthash(H256_from_bytes(execution_payload.parent_hash.bytes));
    execution_payload_grpc.set_allocated_prevrandao(H256_from_bytes(execution_payload.prev_randao.bytes));
    execution_payload_grpc.set_allocated_basefeepergas(H256_from_uint256(execution_payload.base_fee));
    // Logs Bloom
    execution_payload_grpc.set_allocated_logsbloom(H2048_from_bytes(&execution_payload.logs_bloom[0]));
    // String-like parameters
    for (auto transaction_bytes : execution_payload.transactions) {
        execution_payload_grpc.add_transactions(std::string(transaction_bytes.begin(), transaction_bytes.end()));
    }
    execution_payload_grpc.set_extradata(std::string(execution_payload.extra_data.begin(), execution_payload.extra_data.end()));
    return execution_payload_grpc;
}

std::string BackEndGrpc::decode_status_message(const remote::EngineStatus& status) {
    switch (status) {
        case remote::EngineStatus::VALID:
            return "VALID";
        case remote::EngineStatus::SYNCING:
            return "SYNCING";
        case remote::EngineStatus::ACCEPTED:
            return "ACCEPTED";
        case remote::EngineStatus::INVALID_BLOCK_HASH:
            return "INVALID_BLOCK_HASH";
        case remote::EngineStatus::INVALID_TERMINAL_BLOCK:
            return "INVALID_TERMINAL_BLOCK";
        default:
            return "INVALID";
    }
}

} // namespace silkrpc::ethbackend
