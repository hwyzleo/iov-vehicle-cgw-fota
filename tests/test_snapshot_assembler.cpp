#include "snapshot_assembler.h"
#include "someip_fota_client.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace cgw_fota;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class MockSomeIpFotaClient : public SomeIpFotaClient {
public:
    MOCK_METHOD(bool, collectVehicleInventory, (VehicleSoftwareSnapshot& snapshot));
    MOCK_METHOD(bool, getEcuVersion, (const std::string& ecu_id, EcuVersionEntry& entry));
    MOCK_METHOD(bool, getRegistryVersion, (std::string& version));
    MOCK_METHOD(bool, getVin, (std::string& vin));
};

class SnapshotAssemblerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(SnapshotAssemblerTest, AssembleSnapshot) {
    auto mock_client = std::make_shared<MockSomeIpFotaClient>();

    VehicleSoftwareSnapshot expected_snapshot;
    expected_snapshot.vin = "12345678901234567";
    expected_snapshot.overall_result = CollectionStatus::ALL_OK;

    EXPECT_CALL(*mock_client, collectVehicleInventory(_))
        .WillOnce(Invoke([&expected_snapshot](VehicleSoftwareSnapshot& snapshot) {
            snapshot = expected_snapshot;
            return true;
        }));

    SnapshotAssembler assembler(mock_client);
    VehicleSoftwareSnapshot snapshot;
    bool result = assembler.assembleSnapshot(snapshot);

    EXPECT_TRUE(result);
    EXPECT_EQ(snapshot.vin, "12345678901234567");
    EXPECT_EQ(snapshot.snapshot_seq, 1);
}

TEST_F(SnapshotAssemblerTest, AssembleSnapshotWithSeqIncrement) {
    auto mock_client = std::make_shared<MockSomeIpFotaClient>();

    VehicleSoftwareSnapshot snapshot1;
    snapshot1.vin = "12345678901234567";
    snapshot1.overall_result = CollectionStatus::ALL_OK;

    VehicleSoftwareSnapshot snapshot2;
    snapshot2.vin = "12345678901234567";
    snapshot2.overall_result = CollectionStatus::ALL_OK;

    EXPECT_CALL(*mock_client, collectVehicleInventory(_))
        .WillOnce(Invoke([&snapshot1](VehicleSoftwareSnapshot& snapshot) {
            snapshot = snapshot1;
            return true;
        }))
        .WillOnce(Invoke([&snapshot2](VehicleSoftwareSnapshot& snapshot) {
            snapshot = snapshot2;
            return true;
        }));

    SnapshotAssembler assembler(mock_client);
    assembler.setThrottleInterval(0); // Disable throttle for this test

    VehicleSoftwareSnapshot result1;
    assembler.assembleSnapshot(result1);

    VehicleSoftwareSnapshot result2;
    assembler.assembleSnapshot(result2);

    EXPECT_EQ(result1.snapshot_seq, 1);
    EXPECT_EQ(result2.snapshot_seq, 2);
}

TEST_F(SnapshotAssemblerTest, AssembleSnapshotFailure) {
    auto mock_client = std::make_shared<MockSomeIpFotaClient>();

    EXPECT_CALL(*mock_client, collectVehicleInventory(_))
        .WillOnce(Return(false));

    SnapshotAssembler assembler(mock_client);
    VehicleSoftwareSnapshot snapshot;
    bool result = assembler.assembleSnapshot(snapshot);

    EXPECT_FALSE(result);
}

TEST_F(SnapshotAssemblerTest, ThrottleReporting) {
    auto mock_client = std::make_shared<MockSomeIpFotaClient>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.overall_result = CollectionStatus::ALL_OK;

    EXPECT_CALL(*mock_client, collectVehicleInventory(_))
        .WillOnce(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    SnapshotAssembler assembler(mock_client);
    assembler.setThrottleInterval(100); // 100ms throttle

    VehicleSoftwareSnapshot result1;
    assembler.assembleSnapshot(result1);

    // Second call should be throttled
    VehicleSoftwareSnapshot result2;
    bool second_result = assembler.assembleSnapshot(result2);

    EXPECT_FALSE(second_result); // Should be throttled
}
