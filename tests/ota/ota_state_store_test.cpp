// =============================================================================
// tests/ota/ota_state_store_test.cpp
// CGW-FOTA OTA 状态存储单元测试 (CGW-FOTA-DSN-CR-009 §13.3)
// 覆盖：9 类记录 round-trip、删除、proto-hex 辅助、未知新版本 fail-closed。
// =============================================================================

#include "cgw/fota/store/fota_state_store.hpp"
#include "cgw/fota/store/ota_state_serializer.hpp"
#include "cgw/fota/store/ota_state_store.hpp"

#include "vehicle/ota/v1/execution.pb.h"
#include "vehicle/ota/v1/task.pb.h"

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <random>
#include <string>

namespace fs = std::filesystem;
using namespace cgw_fota::store;
using namespace cgw_fota::store::ota;

namespace {
fs::path makeUniqueRoot() {
    static int counter = 0;
    auto root = fs::temp_directory_path() /
                ("fota-ota-store-test-" + std::to_string(getpid()) +
                 "-" + std::to_string(counter++));
    fs::remove_all(root);
    fs::create_directories(root);
    return root;
}

void corruptKeyFile(const fs::path& root, const std::string& key) {
    fs::path file = root / "fota" / (key + ".dat");
    ASSERT_TRUE(fs::exists(file)) << file;
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    std::mt19937 rng(7);
    for (int i = 0; i < 64; ++i) out.put(static_cast<char>(rng() & 0xFF));
}
} // namespace

// ---------------------------------------------------------------------------
// proto-binary <-> hex 辅助
// ---------------------------------------------------------------------------
TEST(OtaStateSerializer, ProtoHexRoundTrip) {
    ::vehicle::ota::v1::FrozenTaskSnapshot snap;
    snap.set_vehicle_task_id("VT-001");
    snap.set_task_revision("rev-1");
    snap.set_target_baseline_id("BASE-001");
    snap.set_plan_version("plan-1");
    snap.set_frozen_at_ms(123);
    snap.mutable_time_window()->set_start_time_ms(100);
    snap.mutable_time_window()->set_end_time_ms(200);

    std::string bin;
    ASSERT_TRUE(snap.SerializeToString(&bin));
    std::string hex = protoBinaryToHex(bin);
    ASSERT_FALSE(hex.empty());
    std::string back = hexToProtoBinary(hex);
    EXPECT_EQ(back, bin);

    ::vehicle::ota::v1::FrozenTaskSnapshot snap2;
    ASSERT_TRUE(snap2.ParseFromString(back));
    EXPECT_EQ(snap2.vehicle_task_id(), "VT-001");
    EXPECT_EQ(snap2.task_revision(), "rev-1");
    EXPECT_EQ(snap2.time_window().start_time_ms(), 100);
}

TEST(OtaStateSerializer, HexOddLengthFails) {
    EXPECT_THROW(hexToProtoBinary("abc"), StateDecodeError);
    EXPECT_THROW(hexToProtoBinary("xy"), StateDecodeError);
}

// ---------------------------------------------------------------------------
// 9 类记录 round-trip（通过真实 store）
// ---------------------------------------------------------------------------
class OtaStateStoreTest : public ::testing::Test {
protected:
    fs::path root = makeUniqueRoot();
    std::shared_ptr<FotaStateStore> fotaStore;
    std::unique_ptr<OtaStateStore> otaStore;

    void SetUp() override {
        auto s = FotaStateStore::open(root, 100, 3600000);
        fotaStore = std::make_shared<FotaStateStore>(std::move(s));
        otaStore = std::make_unique<OtaStateStore>(fotaStore->underlyingStore());
    }
};

TEST_F(OtaStateStoreTest, VehicleTaskRoundTrip) {
    EXPECT_FALSE(otaStore->loadVehicleTask().has_value());
    OtaVehicleTaskRecord r;
    r.vehicleTaskId = "VT-001";
    r.taskRevision = "rev-1";
    r.targetBaselineId = "BASE-001";
    r.vehicleTaskState = "DISCOVERED";
    r.frozenAtMs = 1000;
    r.frozenSnapshotPbHex = "0a0356" "5401"; // 占位 hex
    r.superseded = false;
    otaStore->saveVehicleTask(r);

    auto loaded = otaStore->loadVehicleTask();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->vehicleTaskId, "VT-001");
    EXPECT_EQ(loaded->taskRevision, "rev-1");
    EXPECT_EQ(loaded->vehicleTaskState, "DISCOVERED");
    EXPECT_FALSE(loaded->superseded);

    otaStore->removeVehicleTask();
    EXPECT_FALSE(otaStore->loadVehicleTask().has_value());
}

TEST_F(OtaStateStoreTest, InventoryRoundTrip) {
    OtaInventoryRecord r;
    r.mode = "FULL";
    r.inventoryRevision = "inv-rev-1";
    r.algorithm = "sha-256";
    r.ecuListDigest = "abc123";
    r.collectedAtMs = 5000;
    r.fullRequired = true;
    otaStore->saveInventory(r);
    auto loaded = otaStore->loadInventory();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->mode, "FULL");
    EXPECT_EQ(loaded->inventoryRevision, "inv-rev-1");
    EXPECT_TRUE(loaded->fullRequired);
}

TEST_F(OtaStateStoreTest, ConsentRoundTrip) {
    OtaConsentRecord r;
    r.vehicleTaskId = "VT-001";
    r.effectiveStatus = "ACCEPTED";
    r.receiptId = "RCP-001";
    r.receiptExpiresAtMs = 99999;
    r.termsId = "T-1";
    r.termsVersion = "v1";
    otaStore->saveConsent(r);
    auto loaded = otaStore->loadConsent();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->effectiveStatus, "ACCEPTED");
    EXPECT_EQ(loaded->receiptId, "RCP-001");
}

TEST_F(OtaStateStoreTest, DownloadsRoundTrip) {
    OtaDownloadsRecord r;
    r.allReady = false;
    r.packageManifestDigestHex = "deadbeef";
    r.packageManifestAlgorithm = "sha-256";
    OtaDownloadEntry e;
    e.packageId = "PKG-001";
    e.packageRevision = "prev-1";
    e.etag = "etag-1";
    e.offset = 1024;
    e.verifyStatus = "SUCCEEDED";
    e.stageResultId = "SR-001";
    e.stageResultDigest = "digest-1";
    e.ready = true;
    r.entries.push_back(e);
    otaStore->saveDownloads(r);
    auto loaded = otaStore->loadDownloads();
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->entries.size(), 1u);
    EXPECT_EQ(loaded->entries[0].packageId, "PKG-001");
    EXPECT_EQ(loaded->entries[0].offset, 1024);
    EXPECT_TRUE(loaded->entries[0].ready);
}

TEST_F(OtaStateStoreTest, ExecutionRoundTrip) {
    OtaExecutionRecord r;
    r.vehicleTaskId = "VT-001";
    r.executionId = "EX-001";
    r.attemptNo = 1;
    r.permitId = "PMT-001";
    r.permitToken = "tok";
    r.controlRevision = "CR-1";
    r.validUntilMs = 8000;
    r.executionState = "INSTALL";
    r.stage = "INSTALL";
    r.progressPercent = 50;
    r.acceptedSequenceNo = 3;
    r.nextSequenceNo = 5;
    otaStore->saveExecution(r);
    auto loaded = otaStore->loadExecution();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->executionId, "EX-001");
    EXPECT_EQ(loaded->attemptNo, 1u);
    EXPECT_EQ(loaded->executionState, "INSTALL");
    EXPECT_EQ(loaded->acceptedSequenceNo, 3u);
}

TEST_F(OtaStateStoreTest, EventOutboxRoundTrip) {
    OtaEventOutboxRecord r;
    r.nextSequenceNo = 4;
    r.acceptedSequenceNo = 2;
    OtaEventOutboxEntry e;
    e.sequenceNo = 3;
    e.eventId = "EVT-003";
    e.eventDigest = "d3";
    e.stage = "INSTALL";
    e.result = "SUCCEEDED";
    e.timestampMs = 1234;
    e.payloadSummary = "50%";
    e.sendStatus = "PENDING";
    e.eventPbHex = "0a03455654";
    r.entries.push_back(e);
    otaStore->saveEventOutbox(r);
    auto loaded = otaStore->loadEventOutbox();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->nextSequenceNo, 4u);
    EXPECT_EQ(loaded->acceptedSequenceNo, 2u);
    ASSERT_EQ(loaded->entries.size(), 1u);
    EXPECT_EQ(loaded->entries[0].eventId, "EVT-003");
    EXPECT_EQ(loaded->entries[0].sendStatus, "PENDING");
}

TEST_F(OtaStateStoreTest, ControlsRoundTrip) {
    OtaControlsRecord r;
    r.lastAppliedRevision = "CR-2";
    OtaControlEntry e;
    e.controlRevision = "CR-2";
    e.commandType = "PAUSE";
    e.applyMode = "AT_SAFE_POINT";
    e.expiresAtMs = 9000;
    e.ackStatus = "APPLIED";
    e.ackSequenceNo = 1;
    r.entries.push_back(e);
    otaStore->saveControls(r);
    auto loaded = otaStore->loadControls();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->lastAppliedRevision, "CR-2");
    ASSERT_EQ(loaded->entries.size(), 1u);
    EXPECT_EQ(loaded->entries[0].commandType, "PAUSE");
}

TEST_F(OtaStateStoreTest, PolicyRoundTrip) {
    OtaPolicyRecord r;
    r.basePreferenceVersion = "pref-0";
    r.preferenceVersion = "pref-1";
    r.effectivePolicyPbHex = "0a01";
    r.conflict = false;
    otaStore->savePolicy(r);
    auto loaded = otaStore->loadPolicy();
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->preferenceVersion, "pref-1");
    EXPECT_FALSE(loaded->conflict);
}

TEST_F(OtaStateStoreTest, LogJobsRoundTrip) {
    OtaLogJobsRecord r;
    OtaLogJobEntry e;
    e.logRequestId = "LOG-001";
    e.objectKey = "obj-1";
    e.digestHex = "abc";
    e.sizeBytes = 4096;
    e.status = "UPLOADED";
    e.completedAtMs = 7777;
    r.entries.push_back(e);
    otaStore->saveLogJobs(r);
    auto loaded = otaStore->loadLogJobs();
    ASSERT_TRUE(loaded.has_value());
    ASSERT_EQ(loaded->entries.size(), 1u);
    EXPECT_EQ(loaded->entries[0].logRequestId, "LOG-001");
    EXPECT_EQ(loaded->entries[0].sizeBytes, 4096);
}

// ---------------------------------------------------------------------------
// 损坏 fail-closed
// ---------------------------------------------------------------------------
TEST_F(OtaStateStoreTest, CorruptedExecutionFailsClosed) {
    OtaExecutionRecord r;
    r.executionId = "EX-001";
    r.executionState = "READY";
    r.vehicleTaskId = "VT-1";
    r.permitId = "p";
    r.permitToken = "t";
    r.controlRevision = "c";
    otaStore->saveExecution(r);
    ASSERT_TRUE(otaStore->loadExecution().has_value());

    corruptKeyFile(root, ota::keys::EXECUTION);
    // 损坏在框架层或 FOTA 层被检测均属 fail-closed；不返回伪记录。
    EXPECT_ANY_THROW(otaStore->loadExecution());
}

// ---------------------------------------------------------------------------
// 序列化器：未知新版本 fail-closed
// ---------------------------------------------------------------------------
TEST(OtaStateSerializerStandalone, UnknownNewerVersionFails) {
    // 手工构造一个 schemaVersion=2 的 envelope（当前 EXECUTION 为 v1）。
    // 复用 encodeExecution 但篡改 schemaVersion 字段较繁琐；直接构造坏 magic 不可行。
    // 这里用一个合法 v1 envelope 后修改 schemaVersion 字节验证 decode 拒绝。
    OtaExecutionRecord r;
    r.executionId = "EX-X";
    r.executionState = "READY";
    r.vehicleTaskId = "VT-X";
    r.permitId = "p";
    r.permitToken = "t";
    r.controlRevision = "c";
    std::string bytes = encodeExecution(r);
    // envelope: MAGIC(4) schemaVersion(4@off4) ... 篡改 schemaVersion 为 2。
    ASSERT_GE(bytes.size(), 8u);
    bytes[4] = 2; bytes[5] = 0; bytes[6] = 0; bytes[7] = 0;
    EXPECT_THROW(decodeExecution(bytes), StateDecodeError);
}
