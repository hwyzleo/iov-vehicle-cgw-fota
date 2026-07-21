#include "inventory_reporter.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

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
    MOCK_METHOD(bool, assembleSnapshot, (const std::string& vin, VehicleSoftwareSnapshot& snapshot));
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

    EXPECT_CALL(*mock_assembler, assembleSnapshot("12345678901234567", _))
        .WillOnce(Invoke([&snapshot](const std::string& vin, VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillOnce(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory("12345678901234567");

    EXPECT_TRUE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryWithRetry) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    EXPECT_CALL(*mock_assembler, assembleSnapshot("12345678901234567", _))
        .WillOnce(Invoke([&snapshot](const std::string& vin, VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventoryWithRetry(_, 3, 1000))
        .WillOnce(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    reporter.setRetryPolicy(3, 1000);

    bool result = reporter.reportInventory("12345678901234567");

    EXPECT_TRUE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryAssemblyFailure) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot("12345678901234567", _))
        .WillOnce(Return(false));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory("12345678901234567");

    EXPECT_FALSE(result);
}

TEST_F(InventoryReporterTest, ReportInventoryTransmissionFailure) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";

    EXPECT_CALL(*mock_assembler, assembleSnapshot("12345678901234567", _))
        .WillOnce(Invoke([&snapshot](const std::string& vin, VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillOnce(Return(false));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    bool result = reporter.reportInventory("12345678901234567");

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

    EXPECT_CALL(*mock_assembler, assembleSnapshot("12345678901234567", _))
        .WillOnce(Invoke([&snapshot1](const std::string& vin, VehicleSoftwareSnapshot& s) {
            s = snapshot1;
            return true;
        }))
        .WillOnce(Invoke([&snapshot2](const std::string& vin, VehicleSoftwareSnapshot& s) {
            s = snapshot2;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .Times(2)
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);
    reporter.setDedupWindowSize(1);

    // First report should succeed
    bool result1 = reporter.reportInventory("12345678901234567");
    EXPECT_TRUE(result1);

    // Second report with different seq should succeed
    bool result2 = reporter.reportInventory("12345678901234567");
    EXPECT_TRUE(result2);
}
