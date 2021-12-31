/*
   Copyright 2021 The Silkrpc Authors

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

#include "evm_tracing.hpp"

#include <string>

#include <gmock/gmock.h>

#include <asio/co_spawn.hpp>
#include <asio/thread_pool.hpp>
#include <asio/use_future.hpp>
#include <catch2/catch.hpp>

#include <silkrpc/common/log.hpp>
#include <silkrpc/common/util.hpp>
#include <silkrpc/core/rawdb/accessors.hpp>
#include <silkrpc/ethdb/tables.hpp>
#include <silkrpc/types/transaction.hpp>

namespace silkrpc::trace {

using Catch::Matchers::Message;
using evmc::literals::operator""_address;

using testing::_;
using testing::InvokeWithoutArgs;

static silkworm::Bytes kZeroKey{*silkworm::from_hex("0000000000000000")};
static silkworm::Bytes kZeroHeader{*silkworm::from_hex("bf7e331f7f7c1dd2e05159666b3bf8bc7a8a3a9eb1d518969eab529dd9b88c1a")};

static silkworm::Bytes kKey{
    *silkworm::from_hex("00000000005279a7c07964f1077d2856a353fceafefb64b8ec1bcbefec632b6ce3ddff3798f5b878")};
static silkworm::Bytes kHeader{*silkworm::from_hex(
    "f9025ea012e2a1c61976e189a9a8d6a11661314526a153edb174fd2f44db079ef6fbcb3da01dcc4de8dec75d7aab85b567b6ccd41ad312451b"
    "948a7413f0a142fd40d49347940000000000000000000000000000000000000000a06d37320b6b30d8395610f23a017d2cef0ad7a095c36703"
    "0b2be1a2a276a6e3a6a08d713b44f92bd42c9d5cc2cd03fca0b8241e8fe50a6df40eb25b172510e711d7a01356c1956437b9d1d6d8b94012e9"
    "86db41c3cef5e0b645d4a9834389758aa7d5b90100000000022000000020000000108400000004000840010000000160014000800000000004"
    "0000040000000080004000401010000000102000000000003021000100000000002000080040000c20800000028000108000002004404000a9"
    "00040060080001028010000800100040000820000000100802200000004011400a000008000011000080000000000100200000000000001040"
    "90000000000000000000260000050000028000800000808000208400084100010002400000000000000000000002a000000000000002000000"
    "0021000000001004082000000000022000041000000001001080000000228000000000200000200000000400000008000402835279a78401c9"
    "c380835721a584612b9ab2b861000000000000436f6e73656e5379732048797065726c656467657220426573758be0ef16841d141241253bd4"
    "54fcd818886f4eb0bdb413fbe0327d6d95fc04f82d92e0664538b919c69217a2296f07e4dd7392a1b39bc8845e0f02b93eade70d00a0000000"
    "000000000000000000000000000000000000000000000000000000000088000000000000000007")};
static silkworm::Bytes kBody{*silkworm::from_hex("c784023f92e41bc0")};

static silkworm::Bytes kConfigKey{
    *silkworm::from_hex("bf7e331f7f7c1dd2e05159666b3bf8bc7a8a3a9eb1d518969eab529dd9b88c1a")};
static silkworm::Bytes kConfigValue{*silkworm::from_hex(
    "7b22436861696e4e616d65223a22676f65726c69222c22636861696e4964223a352c22636f6e73656e737573223a22636c69717565222c2268"
    "6f6d657374656164426c6f636b223a302c2264616f466f726b537570706f7274223a747275652c22656970313530426c6f636b223a302c2265"
    "697031353048617368223a22307830303030303030303030303030303030303030303030303030303030303030303030303030303030303030"
    "303030303030303030303030303030303030303030222c22656970313535426c6f636b223a302c22656970313538426c6f636b223a302c2262"
    "797a616e7469756d426c6f636b223a302c22636f6e7374616e74696e6f706c65426c6f636b223a302c2270657465727362757267426c6f636b"
    "223a302c22697374616e62756c426c6f636b223a313536313635312c226265726c696e426c6f636b223a343436303634342c226c6f6e646f6e"
    "426c6f636b223a353036323630352c22636c69717565223a7b22706572696f64223a31352c2265706f6368223a33303030307d7d")};

static silkworm::Bytes kAccountHistoryKey1{*silkworm::from_hex("e0a2bd4258d2768837baa26a28fe71dc079f84c700000000005279a8")};
static silkworm::Bytes kAccountHistoryValue1{*silkworm::from_hex(
    "0100000000000000000000003a300000010000005200c003100000008a5e905e9c5ea55ead5eb25eb75ebf5ec95ed25ed75ee15eed5ef25efa"
    "5eff5e085f115f1a5f235f2c5f355f3e5f475f505f595f625f6b5f745f7c5f865f8f5f985fa15faa5faf5fb45fb95fc15fce5fd75fe05fe65f"
    "f25ffb5f04600d6016601f602860306035603a6043604c6055605a606760706079607f608b6094609d60a560af60b860c160ca60d360db60e5"
    "60ee60f460fb600061096111611b6124612d6136613f61486151615a6160616c6175617e6187618f619961a261ab61b461bb61c661cc61d861"
    "e161ea61f361fc6102620e6217622062296230623a6241624d6256625f6271627a6283628c6295629e62a762b062b662c262ca62d462dc62e6"
    "62ef62f86201630a6313631c6325632e63376340634963526358635e6364636a6374637d6388639663a163ac63b563bc63c463ca63d063d963"
    "df63e563eb63f163fd6306640d641264186421642a6433643c6445644c64516457645d64646469647264776484648d6496649f64a864b164c3"
    "64cc64d164de64e764ee64f96402650b6514651d6526652f6538653d6547654d6553655c6565656e657765886592659b65a465ad65b665bf65"
    "c665cb65d165da65e365ec65f565fe6507661066196622662b6634663d6646664f6658666166676672667c6685668e669766a066a966b266bb"
    "66c466ca66d666df66e866f166f766fc6603670c6715671e6724673067386742674b67516757675d6766676f67786781678a678f6796679c67"
    "a167ae67b767c067c967d267e167ed67f667ff67086810681a682368296835683e684768506859685e686b6874687d6886688f689868a168aa"
    "68b368bc68c568ce68e068e968f268fb6804690d6916691f69266931693a6943694c6955695e6967697069796982698b6994699d69a669af69"
    "b869c169ca69d369dc69e569ee69f769fe69086a126a1b6a246a366a3f6a486a516a5a6a636a6c6a7e6a876a906a966aa26aab6ab46abd6ac6"
    "6acf6ad56adb6ae16aea6af36afc6a056b0e6b176b206b296b326b3b6b416b4b6b566b5c6b676b716b7a6b806b886b956b9e6ba76bb06bb96b"
    "bf6bc56bcb6bd06bd56bdd6be66bef6b016c0a6c136c1c6c226c2d6c346c406c496c526c5a6c646c6d6c766c7f6c886c916c966c9c6ca36cac"
    "6cb56cbe6cc76cd06cd96ce26ce86cf16cfd6c066d0f6d186d216d2a6d336d3c6d456d4e6d576d606d696d726d7b6d846d8a6d966d9f6da46d"
    "b16dba6dc36dcc6dd56dde6de76df06df76d026e096e146e1d6e266e2f6e386e416e4a6e516e5c6e656e6c6e746e806e896e906e9b6ea46ead"
    "6eb76ebf6ec86ed16eda6ee36eec6ef56efe6e076f0d6f196f226f2b6f346f3d6f466f4f6f586f616f666f706f776f7c6f856f8e6f976f9e6f"
    "a96fb26fb96fc46fcd6fd66fdc6fe36fe86ff16ffa6fff6f0c7015701e702670397042704b70527058705d7066706f70787081708a7093709a"
    "70a570ae70b770c070c970d170d970e470ed70f670fc7008711271197123712c7135713e714771597162716b7174717a7186718f719871a171"
    "aa71b371bc71c571ce71d771e071f271fb7104720d7216721f72287231723a7240724c7255725b7267726d7279728272897291729d72a672ac"
    "72b872c072ca72d372d972e572ee72f772007309731273197324732d7336733f73487351735a7363736c7375737a73877390739973a273ab73"
    "b473bd73c673cf73d873e173e773ee73f373fc7305740a7419742074297432743b7444744d7456745f746b747174797483748c7495749e74a7"
    "74b074b974c274cb74d074d774dd74e374ee74f87401750a7513751c7525752e7537754975527558755f7564756d7576757f75877591759a75"
    "a375aa75af75b575be75c775d075d975e275eb75f475fd750676187621762a7632763c7645764d7657766076697672767b7683768d7696769f"
    "76a876b176ba76c376cc76d576de76e776f076f97602770b7714771d7724772f77387741774a7753775c7765776e7774778077897792779b77"
    "a477aa77b177b677bf77c577d177da77e377e977f077f577fe7707781078197822782b7834783d7846784f78587861786a7873787c7885788e"
    "789778a078a878b178b878c478cd78d678df78e878f178fa7803790c7915791e7924793079387942794b7954795d7963796e79787981798979"
    "8e7993799879a579ab79b779c079c979d279db79e479ed79f679ff79087a117a1a7a237a2b7a357a3c7a447a507a597a627a6b7a747a7d7a86"
    "7a8f7a987aa17aaa7ab37abc7ac57ace7ad57ae07ae97aee7af87a017b0d7b167b1f7b287b2e7b377b3e7b437b4c7b557b5e7b677b707b797b"
    "827b8b7b947b9a7ba37baf7bb57bbc7bc17bca7bd37bdc7be47bea7bf57b007c097c0f7c197c217c2d7c367c3f7c487c517c5a7c637c6c7c75"
    "7c7e7c877c907c997ca17cab7cb27cb87cbd7cc67ccf7cd67ce17cea7cf37cfc7c057d0b7d177d207d297d317d3b7d417d4d7d537d5f7d657d"
    "6d7d777d837d8c7d957d9e7da77db07db97dc27dcb7dd27dd77ddd7de67def7df87d017e0a7e137e1c7e257e2e7e377e407e497e4f7e557e5b"
    "7e647e6a7e727e7f7e887e917e967ea37eac7eb57ebe7ec77ed07ed67ee27eeb7ef47efd7e037f0f7f187f217f2a7f337f3c7f457f4e7f577f"
    "607f667f727f7b7f847f8d7f")};

static silkworm::Bytes kAccountChangeSetKey{*silkworm::from_hex("00000000005279ab")};
static silkworm::Bytes kAccountChangeSetSubkey{*silkworm::from_hex("e0a2bd4258d2768837baa26a28fe71dc079f84c7")};
static silkworm::Bytes kAccountChangeSetValue{*silkworm::from_hex("030203430b141e903194951083c424fd")};

static silkworm::Bytes kAccountHistoryKey2{*silkworm::from_hex("52728289eba496b6080d57d0250a90663a07e55600000000005279a8")};
static silkworm::Bytes kAccountHistoryValue2{*silkworm::from_hex("0100000000000000000000003a300000010000004e00000010000000d63b")};

static silkworm::Bytes kPlainStateKey1{*silkworm::from_hex("e0a2bd4258d2768837baa26a28fe71dc079f84c7")};
static silkworm::Bytes kPlainStateKey2{*silkworm::from_hex("52728289eba496b6080d57d0250a90663a07e556")};

class EvmTracingMockDatabaseReader : public core::rawdb::DatabaseReader {
  public:
    MOCK_CONST_METHOD2(get, asio::awaitable<KeyValue>(const std::string&, const silkworm::ByteView&));
    MOCK_CONST_METHOD2(get_one, asio::awaitable<silkworm::Bytes>(const std::string&, const silkworm::ByteView&));
    MOCK_CONST_METHOD3(get_both_range, asio::awaitable<std::optional<silkworm::Bytes>>(const std::string&, const silkworm::ByteView&, const silkworm::ByteView&));
    MOCK_CONST_METHOD4(walk, asio::awaitable<void>(const std::string&, const silkworm::ByteView&, uint32_t, core::rawdb::Walker));
    MOCK_CONST_METHOD3(for_prefix, asio::awaitable<void>(const std::string&, const silkworm::ByteView&, core::rawdb::Walker));
};

TEST_CASE("TraceExecutor::execute") {
    SILKRPC_LOG_STREAMS(null_stream(), null_stream());
    SILKRPC_LOG_VERBOSITY(LogLevel::None);

    EvmTracingMockDatabaseReader db_reader;
    asio::thread_pool workers{1};

    ChannelFactory channel_factory = []() {
        return grpc::CreateChannel("localhost", grpc::InsecureChannelCredentials());
    };
    ContextPool context_pool{1, channel_factory};
    auto pool_thread = std::thread([&]() { context_pool.run(); });

    SECTION("Call: failed with intrinsic gas too low") {
        EXPECT_CALL(db_reader, get_one(db::table::kCanonicalHashes, silkworm::ByteView{kZeroKey}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<silkworm::Bytes> {
                co_return kZeroHeader;
            }));
        EXPECT_CALL(db_reader, get(db::table::kConfig, silkworm::ByteView{kConfigKey}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kConfigKey, kConfigValue};
            }));
        EXPECT_CALL(db_reader, get(db::table::kAccountHistory, silkworm::ByteView{kAccountHistoryKey1}))
            .WillRepeatedly(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kAccountHistoryKey1, kAccountHistoryValue1};
            }));
        EXPECT_CALL(db_reader, get(db::table::kAccountHistory, silkworm::ByteView{kAccountHistoryKey2}))
            .WillRepeatedly(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kAccountHistoryKey2, kAccountHistoryValue2};
            }));
        EXPECT_CALL(db_reader, get_one(db::table::kPlainState, silkworm::ByteView{kPlainStateKey1}))
            .WillRepeatedly(InvokeWithoutArgs([]() -> asio::awaitable<silkworm::Bytes> {
                co_return silkworm::Bytes{};
            }));

        const auto block_number = 5'405'095; // 0x5279A7
        silkrpc::Call call;
        call.from = 0xe0a2Bd4258D2768837BAa26A28fE71Dc079f84c7_address;
        call.gas = 50'000;
        call.gas_price = 7;
        call.data = *silkworm::from_hex("602a60005500");

        silkworm::Block block{};
        block.header.number = block_number;

        TraceExecutor executor{context_pool.get_context(), db_reader, workers};
        auto execution_result = asio::co_spawn(context_pool.get_io_context().get_executor(), executor.execute(block, call), asio::use_future);
        auto result = execution_result.get();
        context_pool.stop();
        pool_thread.join();

        CHECK(result.pre_check_error.has_value() == true);
        CHECK(result.pre_check_error.value() == "tracing failed: intrinsic gas too low: have 50000 want 53072");
    }

    SECTION("Call: succedeed") {
        EXPECT_CALL(db_reader, get_one(db::table::kCanonicalHashes, silkworm::ByteView{kZeroKey}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<silkworm::Bytes> {
                co_return kZeroHeader;
            }));
        EXPECT_CALL(db_reader, get(db::table::kConfig, silkworm::ByteView{kConfigKey}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kConfigKey, kConfigValue};
            }));
        EXPECT_CALL(db_reader, get(db::table::kAccountHistory, silkworm::ByteView{kAccountHistoryKey1}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kAccountHistoryKey1, kAccountHistoryValue1};
            }));
        EXPECT_CALL(db_reader, get_both_range(db::table::kPlainAccountChangeSet, silkworm::ByteView{kAccountChangeSetKey}, silkworm::ByteView{kAccountChangeSetSubkey}))
            .WillOnce(InvokeWithoutArgs([]() -> asio::awaitable<std::optional<silkworm::Bytes>> {
                co_return kAccountChangeSetValue;
            }));
        EXPECT_CALL(db_reader, get(db::table::kAccountHistory, silkworm::ByteView{kAccountHistoryKey2}))
            .WillRepeatedly(InvokeWithoutArgs([]() -> asio::awaitable<KeyValue> {
                co_return KeyValue{kAccountHistoryKey2, kAccountHistoryValue2};
            }));
        EXPECT_CALL(db_reader, get_one(db::table::kPlainState, silkworm::ByteView{kPlainStateKey2}))
            .WillRepeatedly(InvokeWithoutArgs([]() -> asio::awaitable<silkworm::Bytes> {
                co_return silkworm::Bytes{};
            }));

        const auto block_number = 5'405'095; // 0x5279A7
        silkrpc::Call call;
        call.from = 0xe0a2Bd4258D2768837BAa26A28fE71Dc079f84c7_address;
        call.gas = 118'936;
        call.gas_price = 7;
        call.data = *silkworm::from_hex("602a60005500");

        silkworm::Block block{};
        block.header.number = block_number;

        TraceExecutor executor{context_pool.get_context(), db_reader, workers};
        auto execution_result = asio::co_spawn(context_pool.get_io_context().get_executor(), executor.execute(block, call), asio::use_future);
        auto result = execution_result.get();
        context_pool.stop();
        pool_thread.join();

        CHECK(result.pre_check_error.has_value() == false);

        CHECK(result.trace == R"({
            "failed": false,
            "gas": 75178,
            "returnValue": "",
            "structLogs": [
                {
                    "depth": 1,
                    "gas": 65864,
                    "gasCost": 3,
                    "memory": [],
                    "op": "PUSH1",
                    "pc": 0,
                    "stack": []
                },
                {
                    "depth": 1,
                    "gas": 65861,
                    "gasCost": 3,
                    "memory": [],
                    "op": "PUSH1",
                    "pc": 2,
                    "stack": [
                        "0x2a"
                    ]
                },
                {
                    "depth": 1,
                    "gas": 65858,
                    "gasCost": 22100,
                    "memory": [],
                    "op": "SSTORE",
                    "pc": 4,
                    "stack": [
                        "0x2a",
                        "0x0"
                    ],
                    "storage": {
                        "0000000000000000000000000000000000000000000000000000000000000000": "000000000000000000000000000000000000000000000000000000000000002a"
                    }
                },
                {
                    "depth": 1,
                    "gas": 43758,
                    "gasCost": 0,
                    "memory": [],
                    "op": "STOP",
                    "pc": 5,
                    "stack": []
                }
            ]
        })"_json);
    }
}

TEST_CASE("Trace json serialization") {
    SILKRPC_LOG_STREAMS(null_stream(), null_stream());
    SILKRPC_LOG_VERBOSITY(LogLevel::None);

    TraceLog log;
    log.pc = 1;
    log.op = "PUSH1";
    log.gas = 3;
    log.gas_cost = 4;
    log.depth = 1;
    log.error = false;
    log.memory.push_back("0000000000000000000000000000000000000000000000000000000000000080");
    log.stack.push_back("0x80");
    log.storage["804292fe56769f4b9f0e91cf85875f67487cd9e85a084cbba2188be4466c4f23"] = "0000000000000000000000000000000000000000000000000000000000000008";

    SECTION("Trace: no memory, stack and storage") {
        Trace trace;
        trace.failed = false;
        trace.gas = 20;
        trace.return_value = "deadbeaf";
        trace.trace_logs.push_back(log);

        trace.trace_config.disableStorage = true;
        trace.trace_config.disableMemory = true;
        trace.trace_config.disableStack = true;

        CHECK(trace == R"({
            "failed": false,
            "gas": 20,
            "returnValue": "deadbeaf",
            "structLogs": [{
                "depth": 1,
                "gas": 3,
                "gasCost": 4,
                "op": "PUSH1",
                "pc": 1
            }]
        })"_json);
    }

    SECTION("Trace: only memory") {
        Trace trace;
        trace.failed = false;
        trace.gas = 20;
        trace.return_value = "deadbeaf";
        trace.trace_logs.push_back(log);

        trace.trace_config.disableStorage = true;
        trace.trace_config.disableMemory = false;
        trace.trace_config.disableStack = true;

        CHECK(trace == R"({
            "failed": false,
            "gas": 20,
            "returnValue": "deadbeaf",
            "structLogs": [{
                "depth": 1,
                "gas": 3,
                "gasCost": 4,
                "op": "PUSH1",
                "pc": 1,
                "memory":["0000000000000000000000000000000000000000000000000000000000000080"]
            }]
        })"_json);
    }

    SECTION("Trace: only stack") {
        Trace trace;
        trace.failed = false;
        trace.gas = 20;
        trace.return_value = "deadbeaf";
        trace.trace_logs.push_back(log);

        trace.trace_config.disableStorage = true;
        trace.trace_config.disableMemory = true;
        trace.trace_config.disableStack = false;

        CHECK(trace == R"({
            "failed": false,
            "gas": 20,
            "returnValue": "deadbeaf",
            "structLogs": [{
                "depth": 1,
                "gas": 3,
                "gasCost": 4,
                "op": "PUSH1",
                "pc": 1,
                "stack":["0x80"]
            }]
        })"_json);
    }

    SECTION("Trace: only storage") {
        Trace trace;
        trace.failed = false;
        trace.gas = 20;
        trace.return_value = "deadbeaf";
        trace.trace_logs.push_back(log);

        trace.trace_config.disableStorage = false;
        trace.trace_config.disableMemory = true;
        trace.trace_config.disableStack = true;

        CHECK(trace == R"({
            "failed": false,
            "gas": 20,
            "returnValue": "deadbeaf",
            "structLogs": [{
                "depth": 1,
                "gas": 3,
                "gasCost": 4,
                "op": "PUSH1",
                "pc": 1,
                "storage": {
                    "804292fe56769f4b9f0e91cf85875f67487cd9e85a084cbba2188be4466c4f23": "0000000000000000000000000000000000000000000000000000000000000008"
                }
            }]
        })"_json);
    }
}

TEST_CASE("TraceConfig json deserialization") {
    SILKRPC_LOG_STREAMS(null_stream(), null_stream());
    SILKRPC_LOG_VERBOSITY(LogLevel::None);

    nlohmann::json json = R"({
       "disableStorage": true,
       "disableMemory": false,
       "disableStack": true
    })"_json;

    TraceConfig config;
    from_json(json, config);

    CHECK(config.disableStorage == true);
    CHECK(config.disableMemory == false);
    CHECK(config.disableStack == true);
}
}  // namespace silkrpc::trace
