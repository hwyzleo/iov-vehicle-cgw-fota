#include "inventory_reporter.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class MockSomeIpTboxClient : public SomeIpTboxClient {
public:
    MOCK_METHOD(bool, reportSoftwareInventory, (const VehicleSoftwareSnapshot& snapshot));
    MOCK_METHOD(bool, reportSoftwareInventoryWithRetry, (const VehicleSoftwareSnapshot& snapshot,
                                                       uint32_t max_retries,
                                                       uint32_t retry_interval_ms));
};

class MockSnapshotAssembler : public SnapshotAssembler {
public:
    MockSnapshotAssembler() : SnapshotAssembler(nullptr) {}
    MOCK_METHOD(bool, assembleSnapshot, (VehicleSoftwareSnapshot& snapshot));
};

class InventoryReporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(InventoryReporterTest, ReportInventory) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillOnce(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillOnce(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory();

    EXPECT_TRUE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryWithRetry) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillOnce(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventoryWithRetry(_, 3, 1000))
        .WillOnce(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    reporter.setRetryPolicy(3, 1000);

    bool result = reporter.reportInventory();

    EXPECT_TRUE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryAssemblyFailure) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillOnce(Return(false));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory();

    EXPECT_FALSE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryTransmissionFailure) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillOnce(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillOnce(Return(false));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory();

    EXPECT_FALSE(result);
}

TEST_F(InventoryReporterTest, Deduplication) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot1;
    snapshot1.vin = "12345678901234567";
    snapshot1.snapshot_seq = 1;

    VehicleSoftwareSnapshot snapshot2;
    snapshot2.vin = "12345678901234567";
    snapshot2.snapshot_seq = 2;

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillOnce(Invoke([&snapshot1](VehicleSoftwareSnapshot& s) {
            s = snapshot1;
            return true;
        }))
        .WillOnce(Invoke([&snapshot2](VehicleSoftwareSnapshot& s) {
            s = snapshot2;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .Times(2)
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    reporter.setDedupWindowSize(1);

    // First report should succeed
    bool result1 = reporter.reportInventory();
    EXPECT_TRUE(result1);

    // Second report with different seq should succeed
    bool result2 = reporter.reportInventory();
    EXPECT_TRUE(result2);
}

TEST_F(InventoryReporterTest, AsyncReportReturnsImmediateResult) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    // 模拟耗时操作，以便测试合并逻辑
    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            // 模拟采集耗时
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 第一个请求应该被接受
    auto result1 = reporter.reportInventoryAsync("req-001", "cloud_query");
    EXPECT_TRUE(result1.accepted);
    EXPECT_EQ(result1.report_id, 1);

    // 等待一小段时间让异步任务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 第二个请求应该合并到在途任务
    auto result2 = reporter.reportInventoryAsync("req-002", "manual_retry");
    EXPECT_TRUE(result2.accepted);
    EXPECT_EQ(result2.report_id, 1);  // 相同的 reportId

    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(InventoryReporterTest, IsCollectingState) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
            s.vin = "12345678901234567";
            s.snapshot_seq = 1;
            // 模拟耗时操作
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 初始状态不在采集
    EXPECT_FALSE(reporter.isCollecting());

    // 触发异步采集
    reporter.reportInventoryAsync("req-001", "cloud_query");

    // 应该立即变为采集状态
    EXPECT_TRUE(reporter.isCollecting());

    // 等待完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 完成后应该不在采集
    EXPECT_FALSE(reporter.isCollecting());
}

TEST_F(InventoryReporterTest, ReportIdIncrement) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
            s.vin = "12345678901234567";
            s.snapshot_seq = 1;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 触发多个请求（等待前一个完成）
    auto result1 = reporter.reportInventoryAsync("req-001", "cloud_query");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto result2 = reporter.reportInventoryAsync("req-002", "manual_retry");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // reportId 应该递增
    EXPECT_EQ(result1.report_id, 1);
    EXPECT_EQ(result2.report_id, 2);
}
