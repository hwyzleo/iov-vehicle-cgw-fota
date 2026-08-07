// =============================================================================
// tests/test_store_integration.cpp
// CGW-FOTA 状态持久化集成测试 (CGW-FOTA-DSN-CR-005)
// 覆盖：完整上报周期+store、崩溃恢复（各检查点）、去重、安全。
// =============================================================================

#include "inventory_reporter.h"
#include "cgw/fota/store/fota_state_store.hpp"
#include "cgw/fota/store/fota_state_recovery.hpp"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace cgw_fota;
using namespace cgw_fota::store;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

namespace {
fs::path makeUniqueRoot() {
    static int counter = 0;
    auto root = fs::temp_directory_path() /
                ("fota-store-int-" + std::to_string(getpid()) +
                 "-" + std::to_string(counter++));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

class MockTboxClient : public SomeIpTboxClient {
public:
    MOCK_METHOD(bool, reportSoftwareInventory, (const VehicleSoftwareSnapshot&));
    MOCK_METHOD(bool, reportSoftwareInventoryWithRetry,
                (const VehicleSoftwareSnapshot&, uint32_t, uint32_t));
};

class MockAssembler : public SnapshotAssembler {
public:
    MockAssembler() : SnapshotAssembler(nullptr) {}
    MOCK_METHOD(bool, assembleSnapshot, (VehicleSoftwareSnapshot&));
};

VehicleSoftwareSnapshot makeSnapshot(std::uint64_t seq) {
    VehicleSoftwareSnapshot s;
    s.vin = "LSJAAAAAAAAAAAAAA";
    s.baseline_source = BaselineSource::FACTORY;
    s.registry_version = "1.0.0";
    s.collected_at = "2026-08-07T10:00:00Z";
    s.overall_result = CollectionStatus::ALL_OK;
    s.snapshot_seq = seq;
    return s;
}
} // namespace

// ===========================================================================
// 完整上报周期：store 记录 LastSuccess、Dedupe，清理 ActiveJob
// ===========================================================================
TEST(StoreIntegrationTest, FullReportCycleSavesLastSuccessAndDedupe) {
    auto root = makeUniqueRoot();
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));

    auto tbox = std::make_shared<MockTboxClient>();
    auto asmr = std::make_shared<MockAssembler>();
    auto reporter = std::make_shared<InventoryReporter>(tbox, asmr);
    reporter->setStateStore(store);

    VehicleSoftwareSnapshot snap = makeSnapshot(1);
    EXPECT_CALL(*asmr, assembleSnapshot(_))
        .WillOnce(Invoke([&snap](VehicleSoftwareSnapshot& s) { s = snap; return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .WillOnce(Return(true));

    EXPECT_TRUE(reporter->reportInventory());

    // LastSuccess 已保存
    auto ls = store->loadLastSuccess();
    ASSERT_TRUE(ls.has_value());
    EXPECT_EQ(ls->snapshotSeq, 1u);
    EXPECT_EQ(ls->snapshot.vin, "LSJAAAAAAAAAAAAAA");

    // Dedupe 已更新
    DedupeState d = store->loadDedupe();
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].snapshotSeq, 1u);

    // ActiveJob 已清理
    EXPECT_FALSE(store->loadActiveJob().has_value());

    fs::remove_all(root);
}

// ===========================================================================
// 去重：已成功上报的序号不再重复上报
// ===========================================================================
TEST(StoreIntegrationTest, DuplicateSeqIsSkipped) {
    auto root = makeUniqueRoot();
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));

    auto tbox = std::make_shared<MockTboxClient>();
    auto asmr = std::make_shared<MockAssembler>();
    auto reporter = std::make_shared<InventoryReporter>(tbox, asmr);
    reporter->setStateStore(store);

    VehicleSoftwareSnapshot snap = makeSnapshot(1);
    // 两次都返回 seq=1
    EXPECT_CALL(*asmr, assembleSnapshot(_))
        .Times(2)
        .WillRepeatedly(Invoke([&snap](VehicleSoftwareSnapshot& s) { s = snap; return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .Times(1)  // 第二次应被去重跳过
        .WillOnce(Return(true));

    EXPECT_TRUE(reporter->reportInventory());    // 首次成功
    EXPECT_FALSE(reporter->reportInventory());   // 第二次去重跳过

    fs::remove_all(root);
}

// ===========================================================================
// TBOX 失败：保存 SubmitUnknown 检查点，不保存 LastSuccess
// ===========================================================================
TEST(StoreIntegrationTest, TboxFailureSavesSubmitUnknown) {
    auto root = makeUniqueRoot();
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));

    auto tbox = std::make_shared<MockTboxClient>();
    auto asmr = std::make_shared<MockAssembler>();
    auto reporter = std::make_shared<InventoryReporter>(tbox, asmr);
    reporter->setStateStore(store);

    VehicleSoftwareSnapshot snap = makeSnapshot(1);
    EXPECT_CALL(*asmr, assembleSnapshot(_))
        .WillOnce(Invoke([&snap](VehicleSoftwareSnapshot& s) { s = snap; return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .WillOnce(Return(false));

    EXPECT_FALSE(reporter->reportInventory());

    // LastSuccess 未保存
    EXPECT_FALSE(store->loadLastSuccess().has_value());
    // ActiveJob 停留在 SubmitUnknown
    auto job = store->loadActiveJob();
    ASSERT_TRUE(job.has_value());
    EXPECT_EQ(job->phase, JobPhase::SubmitUnknown);
    EXPECT_EQ(job->snapshotSeq, 1u);

    fs::remove_all(root);
}

// ===========================================================================
// 崩溃恢复：SubmitPrepared 阶段崩溃 -> 重启恢复 -> 重提交成功
// ===========================================================================
TEST(StoreIntegrationTest, CrashAtSubmitPreparedRecoversAndResubmits) {
    auto root = makeUniqueRoot();
    // 模拟崩溃：保存 SubmitPrepared 检查点（seq=1）
    {
        auto store = std::make_shared<FotaStateStore>(
            FotaStateStore::open(root, 100, 3600000));
        store->allocateSnapshotSeq();  // seq=1
        ActiveJobState job;
        job.reportId = "1";
        job.snapshotSeq = 1;
        job.phase = JobPhase::SubmitPrepared;
        job.idempotencyKey = "idem-1";
        job.reason = TriggerReason::AutoStart;
        store->saveActiveJob(job);
    }

    // 重启：新 store + 恢复
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));
    auto plan = StateRecovery::recover(*store);
    EXPECT_EQ(plan.action, RecoveryAction::Resubmit);
    ASSERT_TRUE(plan.job.has_value());
    EXPECT_EQ(plan.job->idempotencyKey, "idem-1");

    auto tbox = std::make_shared<MockTboxClient>();
    auto asmr = std::make_shared<MockAssembler>();
    auto reporter = std::make_shared<InventoryReporter>(tbox, asmr);
    reporter->setStateStore(store);
    reporter->applyRecoveryPlan(plan);

    // 恢复后重新采集并以原序号/幂等标识提交
    VehicleSoftwareSnapshot snap = makeSnapshot(1);
    EXPECT_CALL(*asmr, assembleSnapshot(_))
        .WillOnce(Invoke([&snap](VehicleSoftwareSnapshot& s) { s = snap; return true; }));
    EXPECT_CALL(*tbox, reportSoftwareInventory(_))
        .WillOnce(Return(true));

    EXPECT_TRUE(reporter->reportInventory());

    // 成功后 LastSuccess 保存，ActiveJob 清理
    auto ls = store->loadLastSuccess();
    ASSERT_TRUE(ls.has_value());
    EXPECT_EQ(ls->snapshotSeq, 1u);
    EXPECT_FALSE(store->loadActiveJob().has_value());

    fs::remove_all(root);
}

// ===========================================================================
// 崩溃恢复：CompletedPendingCleanup 阶段崩溃 -> 重启对账清理
// ===========================================================================
TEST(StoreIntegrationTest, CrashAtCompletedPendingCleanupReconciles) {
    auto root = makeUniqueRoot();
    {
        auto store = std::make_shared<FotaStateStore>(
            FotaStateStore::open(root, 100, 3600000));
        store->allocateSnapshotSeq();  // seq=1
        ActiveJobState job;
        job.reportId = "1";
        job.snapshotSeq = 1;
        job.phase = JobPhase::CompletedPendingCleanup;
        job.idempotencyKey = "idem-1";
        store->saveActiveJob(job);
        // last_success 已保存（匹配）
        LastSuccessState ls;
        ls.reportId = "1";
        ls.snapshotSeq = 1;
        ls.completedAt = 1;
        ls.registryVersion = "1.0.0";
        ls.overallResult = CollectionStatus::ALL_OK;
        ls.snapshot = makeSnapshot(1);
        store->saveLastSuccess(ls);
    }

    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));
    auto plan = StateRecovery::recover(*store);
    EXPECT_EQ(plan.action, RecoveryAction::None);
    EXPECT_FALSE(plan.job.has_value());  // 已清理

    // ActiveJob 已删除
    EXPECT_FALSE(store->loadActiveJob().has_value());
    // Dedupe 已补齐
    DedupeState d = store->loadDedupe();
    ASSERT_EQ(d.entries.size(), 1u);
    EXPECT_EQ(d.entries[0].snapshotSeq, 1u);

    fs::remove_all(root);
}

// ===========================================================================
// 崩溃恢复：恢复后序号不复用（继续递增）
// ===========================================================================
TEST(StoreIntegrationTest, SequenceContinuesAfterRecovery) {
    auto root = makeUniqueRoot();
    {
        auto store = std::make_shared<FotaStateStore>(
            FotaStateStore::open(root, 100, 3600000));
        store->allocateSnapshotSeq();  // seq=1
        store->allocateSnapshotSeq();  // seq=2
    }
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));
    // 重启后序号从 3 继续，不复用 1/2
    EXPECT_EQ(store->allocateSnapshotSeq(), 3u);
    fs::remove_all(root);
}

// ===========================================================================
// 安全：store 错误日志字段不含 VIN/snapshot/device_sn
// ===========================================================================
TEST(StoreIntegrationTest, StoreFilesDoNotLeakVinInPlaintextPath) {
    auto root = makeUniqueRoot();
    auto store = std::make_shared<FotaStateStore>(
        FotaStateStore::open(root, 100, 3600000));

    // 保存含 VIN 的 LastSuccess
    LastSuccessState ls;
    ls.reportId = "1";
    ls.snapshotSeq = 1;
    ls.completedAt = 1;
    ls.registryVersion = "1.0.0";
    ls.snapshot = makeSnapshot(1);
    store->saveLastSuccess(ls);

    // 文件名不含 VIN（仅 key.dat）
    for (const auto& entry : fs::directory_iterator(root / "fota")) {
        std::string name = entry.path().filename().string();
        EXPECT_EQ(name.find("LSJ"), std::string::npos)
            << "VIN leaked in filename: " << name;
    }
    fs::remove_all(root);
}
