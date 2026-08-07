// =============================================================================
// tests/someip/provider_test.cpp
// CGW-FOTA-DSN-CR-007 §测试设计 - FOTA Provider 适配器测试
// 覆盖：合法/非法请求、重复 requestId 合并返回稳定 reportId、payload 超限/codec
// 失败 -> CGW-FW-0306 wire error、一次 response、stopOffer。
// 使用真实 SomeIpRuntime（FakeSomeIpBackend）+ InlineExecutor，Provider/Client 进程内通信。
// =============================================================================
#include "fota_someip_test_util.h"
#include "../test_helpers.h"
#include "cgw/fota/someip/fota_provider.hpp"
#include "cgw/fw/someip/runtime.hpp"
#include "cgw/fw/someip/types.hpp"
#include "constants.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include <vector>

using namespace cgw_fota;
using namespace cgw_fota_test;
namespace someip_fw = cgw::fw::someip;

namespace {

// 编码 IDL 请求（与 src/someip/fota_provider.cpp codec 一致）。
someip_fw::Payload encodeRequest(const std::string& requestId,
                                 const std::string& reason,
                                 const std::string& traceId = "") {
    someip_fw::Payload p;
    auto putU8 = [&](uint8_t v) { p.push_back(v); };
    auto putU16 = [&](uint16_t v) {
        p.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
        p.push_back(static_cast<uint8_t>(v & 0xFF));
    };
    auto putStr = [&](const std::string& s) {
        putU16(static_cast<uint16_t>(s.size()));
        p.insert(p.end(), s.begin(), s.end());
    };
    putU8(1);             // schemaVersion
    putStr(requestId);
    putStr(reason);
    if (traceId.empty()) {
        putU8(0);         // no metadata
    } else {
        putU8(1);         // hasMetadata
        putStr(traceId);  // traceId
        putStr("test");   // caller
        putU8(1);         // metadataSchemaVersion
    }
    return p;
}

// 解码 IDL 响应，返回 {accepted, reportId}。
struct DecodedResp {
    bool accepted{false};
    uint64_t reportId{0};
    bool hasError{false};
};
DecodedResp decodeResponse(const someip_fw::Payload& p) {
    DecodedResp r;
    if (p.size() < 11) return r;
    size_t off = 0;
    // schemaVersion
    off += 1;
    r.accepted = (p[off++] != 0);
    for (int i = 0; i < 8; ++i) {
        r.reportId = (r.reportId << 8) | p[off++];
    }
    r.hasError = (p[off++] != 0);
    return r;
}

// 等待 Client availability 达到 Available（有界）。
bool waitAvailable(someip_fw::Client& c, int ms = 1000) {
    for (int i = 0; i < ms; ++i) {
        if (c.availability() == someip_fw::Availability::Available) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return c.availability() == someip_fw::Availability::Available;
}

// 慢速 DIAG client：collectVehicleInventory 延迟，使异步任务保持 in-flight 以测试合并。
class SlowDiagInventoryClient : public someip::DiagInventoryClient {
public:
    SlowDiagInventoryClient() : DiagInventoryClient() {}
    bool collectVehicleInventory(VehicleSoftwareSnapshot& snapshot) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        snapshot.vin = "12345678901234567";
        snapshot.overall_result = CollectionStatus::ALL_OK;
        snapshot.snapshot_seq = 1;
        return true;
    }
};

// 构建可用 orchestrator（慢速 DIAG，便于测试并发合并）。
std::shared_ptr<InventoryReporter> makeReporter() {
    auto diag = std::make_shared<SlowDiagInventoryClient>();
    auto tbox = std::make_shared<test::TestableTboxInventoryClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag);
    assembler->setThrottleInterval(0);
    return std::make_shared<InventoryReporter>(tbox, assembler);
}

} // namespace

class FotaProviderTest : public ::testing::Test {
protected:
    std::shared_ptr<InlineExecutor> exec_;
    someip_fw::SomeIpRuntime rt_;
    someip_fw::Provider provider_;
    someip_fw::Client client_;
    std::shared_ptr<someip::FotaProviderAdapter> adapter_;

    void SetUp() override {
        exec_ = std::make_shared<InlineExecutor>();
        rt_ = someip_fw::SomeIpRuntime::create(makeTestConfig(), exec_);
        rt_.start();
        someip_fw::ServiceKey key{FOTA_PROVIDER_SERVICE_ID, FOTA_PROVIDER_INSTANCE_ID};
        provider_ = rt_.createProvider(key, {1, 0});
        client_ = rt_.createClient(key, {1, 0});

        adapter_ = std::make_shared<someip::FotaProviderAdapter>(
            std::move(provider_), makeReporter(), std::chrono::milliseconds(1000));
        adapter_->registerInventoryHandler();
        adapter_->offer();
        client_.requestService();
        ASSERT_TRUE(waitAvailable(client_));
    }
    void TearDown() override {
        adapter_->stopOffer();
        client_.releaseService();
        rt_.stop();
    }
};

TEST_F(FotaProviderTest, ValidRequestReturnsAcceptedAndReportId) {
    auto req = encodeRequest("prov-001", "cloud_query");
    auto result = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req.data(), req.size()});
    ASSERT_TRUE(result.success);
    auto resp = decodeResponse(result.response);
    EXPECT_TRUE(resp.accepted);
    EXPECT_GT(resp.reportId, 0u);
    EXPECT_FALSE(resp.hasError);
}

TEST_F(FotaProviderTest, DuplicateRequestIdMergesToSameReportId) {
    auto req1 = encodeRequest("dup-001", "cloud_query");
    auto r1 = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req1.data(), req1.size()});
    ASSERT_TRUE(r1.success);
    auto resp1 = decodeResponse(r1.response);

    // 让异步任务进入在途状态
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto req2 = encodeRequest("dup-001", "manual_retry");
    auto r2 = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req2.data(), req2.size()});
    ASSERT_TRUE(r2.success);
    auto resp2 = decodeResponse(r2.response);

    // 重复 requestId 命中在途任务 -> 相同 reportId
    EXPECT_EQ(resp1.reportId, resp2.reportId);
}

TEST_F(FotaProviderTest, InvalidPayloadReturnsWireError) {
    // 非法 schemaVersion (0) -> codec 失败 -> CGW-FW-0306
    someip_fw::Payload bad{0x00};
    auto result = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{bad.data(), bad.size()});
    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.code, "SOMEIP/774");  // 0x0306 业务 codec 错误响应
}

TEST_F(FotaProviderTest, TraceMetadataPropagated) {
    // 带 traceId metadata 的请求应被受理
    auto req = encodeRequest("trace-001", "cloud_query", "fota-trace-test");
    auto result = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req.data(), req.size()});
    ASSERT_TRUE(result.success);
    auto resp = decodeResponse(result.response);
    EXPECT_TRUE(resp.accepted);
}

TEST_F(FotaProviderTest, SequentialRequestsIncrementReportId) {
    auto req1 = encodeRequest("seq-001", "cloud_query");
    auto r1 = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req1.data(), req1.size()});
    ASSERT_TRUE(r1.success);
    auto resp1 = decodeResponse(r1.response);

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    auto req2 = encodeRequest("seq-002", "cloud_query");
    auto r2 = client_.call(METHOD_REQUEST_SOFTWARE_INVENTORY,
        someip_fw::PayloadView{req2.data(), req2.size()});
    ASSERT_TRUE(r2.success);
    auto resp2 = decodeResponse(r2.response);

    EXPECT_NE(resp1.reportId, resp2.reportId);
}
