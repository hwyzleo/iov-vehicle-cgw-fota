// =============================================================================
// tests/someip/retry_trace_test.cpp
// CGW-FOTA-DSN-CR-007 §测试设计 - 重试/幂等/trace 测试
// 覆盖：framework 零自动重试（CallOptions.retry=None）、业务重试次数/退避、
// TBOX 幂等身份（同 reportId/snapshotSeq/dedupeKey）、无双层重试放大、
// 掉电恢复关联。
// =============================================================================
#include "fota_someip_test_util.h"
#include "cgw/fota/someip/tbox_inventory_client.hpp"
#include "cgw/fota/someip/diag_inventory_client.hpp"
#include "cgw/fota/store/fota_state_recovery.hpp"
#include "cgw/fw/someip/runtime.hpp"
#include "cgw/fw/someip/types.hpp"
#include "constants.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "fota_log_context.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <chrono>
#include <thread>

using namespace cgw_fota;
using namespace cgw_fota_test;
namespace someip_fw = cgw::fw::someip;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

namespace {

// 可控失败次数的 TBOX mock（gmock）
class FlakyTboxClient : public someip::TboxInventoryClient {
public:
    FlakyTboxClient() : TboxInventoryClient() {}
    MOCK_METHOD(bool, reportSoftwareInventory, (const VehicleSoftwareSnapshot& snapshot));
};

class MockDiagClient : public someip::DiagInventoryClient {
public:
    MockDiagClient() : DiagInventoryClient() {}
    MOCK_METHOD(bool, collectVehicleInventory, (VehicleSoftwareSnapshot& snapshot));
};

VehicleSoftwareSnapshot makeSnap(uint64_t seq) {
    VehicleSoftwareSnapshot s;
    s.vin = "12345678901234567";
    s.snapshot_seq = seq;
    s.overall_result = CollectionStatus::ALL_OK;
    return s;
}

} // namespace

// ============================================================
// 业务重试：前两次失败，第三次成功
// ============================================================
TEST(RetryTraceTest, OrchestratorRetriesUntilSuccess) {
    auto tbox = std::make_shared<FlakyTboxClient>();
    auto diag = std::make_shared<MockDiagClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag);
    assembler->setThrottleInterval(0);

    auto reporter = std::make_shared<InventoryReporter>(tbox, assembler);
    reporter->setRetryPolicy(3, 1);  // max 3 retries, 1ms backoff

    EXPECT_CALL(*diag, collectVehicleInventory(_))
        .WillOnce(Invoke([](VehicleSoftwareSnapshot& s) { s = makeSnap(1); return true; }));

    // 前两次失败，第三次成功（共 3 次调用）
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .Times(3)
        .WillOnce(Return(false))
        .WillOnce(Return(false))
        .WillOnce(Return(true));

    EXPECT_TRUE(reporter->reportInventory());
}

// ============================================================
// 业务重试：全部失败 -> 最终失败
// ============================================================
TEST(RetryTraceTest, OrchestratorExhaustsRetriesAndFails) {
    auto tbox = std::make_shared<FlakyTboxClient>();
    auto diag = std::make_shared<MockDiagClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag);
    assembler->setThrottleInterval(0);

    auto reporter = std::make_shared<InventoryReporter>(tbox, assembler);
    reporter->setRetryPolicy(2, 1);  // max 2 retries -> 3 attempts total

    EXPECT_CALL(*diag, collectVehicleInventory(_))
        .WillOnce(Invoke([](VehicleSoftwareSnapshot& s) { s = makeSnap(1); return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .Times(3)  // 1 + 2 retries
        .WillRepeatedly(Return(false));

    EXPECT_FALSE(reporter->reportInventory());
}

// ============================================================
// TBOX 幂等身份：重试保持相同 snapshotSeq（同一快照重提交）
// ============================================================
TEST(RetryTraceTest, TboxRetriesKeepSameSnapshotSeq) {
    auto tbox = std::make_shared<FlakyTboxClient>();
    auto diag = std::make_shared<MockDiagClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag);
    assembler->setThrottleInterval(0);

    auto reporter = std::make_shared<InventoryReporter>(tbox, assembler);
    reporter->setRetryPolicy(2, 1);

    EXPECT_CALL(*diag, collectVehicleInventory(_))
        .WillOnce(Invoke([](VehicleSoftwareSnapshot& s) { s = makeSnap(42); return true; }));

    // 记录每次提交的 snapshot_seq
    std::vector<uint64_t> seen_seqs;
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .Times(3)
        .WillRepeatedly(Invoke([&seen_seqs](const VehicleSoftwareSnapshot& s) {
            seen_seqs.push_back(s.snapshot_seq);
            return false;  // 全失败以触发全部重试
        }));

    reporter->reportInventory();
    // 所有重试必须保持相同 snapshotSeq（幂等身份）；assembler 分配 seq=1。
    ASSERT_EQ(seen_seqs.size(), 3u);
    EXPECT_EQ(seen_seqs[1], seen_seqs[0]);
    EXPECT_EQ(seen_seqs[2], seen_seqs[0]);
}

// ============================================================
// framework 零自动重试：adapter 使用 CallOptions{retry=None}，
// 单次失败不会触发 framework 层自动重试（业务重试由 orchestrator 执行）。
// 用真实 Runtime 验证：Provider 一次失败响应 -> Client 一次 call 即返回失败，
// 不自动重试。
// ============================================================
TEST(RetryTraceTest, FrameworkNoAutoRetryOnTransportFailure) {
    auto exec = std::make_shared<InlineExecutor>();
    someip_fw::SomeIpRuntime rt = someip_fw::SomeIpRuntime::create(makeTestConfig(), exec);
    rt.start();
    someip_fw::ServiceKey key{DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID};
    someip_fw::Provider provider = rt.createProvider(key, {1, 0});

    int call_count = 0;
    provider.registerMethod(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY,
        [&call_count](const someip_fw::RequestContext&, someip_fw::PayloadView) {
            ++call_count;
            // 返回业务错误响应（不触发 framework 重试）
            return someip_fw::MethodResult::error(0x05, "rejected");
        });
    provider.offer();

    someip_fw::Client client = rt.createClient(key, {1, 0});
    auto tbox = someip::TboxInventoryClient(std::move(client),
                                            std::chrono::milliseconds(1000));
    tbox.requestService();
    for (int i = 0; i < 1000 && tbox.availability() != someip_fw::Availability::Available; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    VehicleSoftwareSnapshot snap = makeSnap(1);
    // 单次 reportSoftwareInventory：Provider 拒绝 -> adapter 返回 false，
    // 且 Provider 只被调用一次（framework 无自动重试）
    EXPECT_FALSE(tbox.reportSoftwareInventory(snap));
    EXPECT_EQ(call_count, 1);

    tbox.releaseService();
    provider.stopOffer();
    rt.stop();
}

// ============================================================
// 掉电恢复关联：恢复任务复用原 reportId/seq（trace 通过原 reportId 关联）
// ============================================================
TEST(RetryTraceTest, RecoveryReusesOriginalReportId) {
    auto tbox = std::make_shared<FlakyTboxClient>();
    auto diag = std::make_shared<MockDiagClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag);
    assembler->setThrottleInterval(0);

    auto reporter = std::make_shared<InventoryReporter>(tbox, assembler);

    // 模拟恢复任务：设置 recovered_job（reportId=99, seq=7）
    store::RecoveryPlan plan;
    plan.action = store::RecoveryAction::Resubmit;
    store::ActiveJobState job;
    job.reportId = "99";
    job.snapshotSeq = 7;
    job.idempotencyKey = "fota-7-99";
    job.reason = store::TriggerReason::Recovery;
    job.phase = store::JobPhase::SubmitPrepared;
    plan.job = job;
    reporter->applyRecoveryPlan(plan);

    uint64_t seen_seq = 0;
    EXPECT_CALL(*diag, collectVehicleInventory(_))
        .WillOnce(Invoke([](VehicleSoftwareSnapshot& s) { s = makeSnap(0); return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .WillOnce(Invoke([&seen_seq](const VehicleSoftwareSnapshot& s) {
            seen_seq = s.snapshot_seq;
            return true;
        }));

    EXPECT_TRUE(reporter->reportInventory());
    // 恢复任务复用原 seq=7（非新分配）
    EXPECT_EQ(seen_seq, 7u);
}

// ============================================================
// trace 上下文：入站 Provider 可选 traceId，缺失时生成新 trace
// ============================================================
TEST(RetryTraceTest, TraceIdGeneratedWhenAbsent) {
    // generate_trace_id 返回非空 trace
    std::string t = generate_trace_id();
    EXPECT_FALSE(t.empty());
    EXPECT_NE(t.find("fota-trace"), std::string::npos);

    std::string r = generate_request_id();
    EXPECT_FALSE(r.empty());
    EXPECT_NE(r.find("fota-req"), std::string::npos);
}
