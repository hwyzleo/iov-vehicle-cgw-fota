#include "test_helpers.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace cgw_fota;
using namespace cgw_fota::test;
using ::testing::_;
using ::testing::Return;

class SomeIpTboxClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(SomeIpTboxClientTest, ConnectToTboxService) {
    // connect() now does real TCP to 127.0.0.1:56101
    SomeIpTboxClient client;
    bool result = client.connect("127.0.0.1", 56101);

    EXPECT_TRUE(result);
    EXPECT_TRUE(client.isConnected());
}

TEST_F(SomeIpTboxClientTest, DisconnectFromTboxService) {
    SomeIpTboxClient client;
    client.connect("127.0.0.1", 56101);

    bool result = client.disconnect();

    EXPECT_TRUE(result);
    EXPECT_FALSE(client.isConnected());
}

TEST_F(SomeIpTboxClientTest, ReportSoftwareInventory) {
    // Use TestableSomeIpTboxClient to avoid real TCP connection in unit test
    TestableSomeIpTboxClient client;
    client.connect("127.0.0.1", 56101);

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    bool result = client.reportSoftwareInventory(snapshot);

    EXPECT_TRUE(result);
    EXPECT_EQ(client.getLastReportedVin(), "12345678901234567");
    EXPECT_EQ(client.getLastReportedSeq(), 1u);
}

TEST_F(SomeIpTboxClientTest, NotConnectedError) {
    SomeIpTboxClient client;
    // Don't connect

    VehicleSoftwareSnapshot snapshot;
    bool result = client.reportSoftwareInventory(snapshot);

    EXPECT_FALSE(result);
}

TEST_F(SomeIpTboxClientTest, ReportWithRetry) {
    // Use TestableSomeIpTboxClient to avoid real TCP connection in unit test
    TestableSomeIpTboxClient client;
    client.connect("127.0.0.1", 56101);

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    // Test retry mechanism
    bool result = client.reportSoftwareInventoryWithRetry(snapshot, 3, 1000);

    EXPECT_TRUE(result);
}
