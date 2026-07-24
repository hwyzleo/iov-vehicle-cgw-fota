#include "test_helpers.h"
#include "data_models.h"
#include <gtest/gtest.h>
#include <iostream>

using namespace cgw_fota;
using namespace cgw_fota::test;

class TboxSomeipIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // 创建 SomeIpTboxClient
        client = std::make_shared<SomeIpTboxClient>();
    }

    void TearDown() override {
        if (client) {
            client->disconnect();
        }
    }

    std::shared_ptr<SomeIpTboxClient> client;
};

TEST_F(TboxSomeipIntegrationTest, ConnectToTboxSomeip) {
    // 测试连接到 tbox-someip 服务（真实 TCP）
    bool connected = client->connect("127.0.0.1", 56101, 0x6101, 0x0001);
    EXPECT_TRUE(connected);
    EXPECT_TRUE(client->isConnected());
}

TEST_F(TboxSomeipIntegrationTest, ReportSoftwareInventory) {
    // 使用 TestableSomeIpTboxClient 避免真实 TCP 连接
    auto testable_client = std::make_shared<TestableSomeIpTboxClient>();
    ASSERT_TRUE(testable_client->connect("127.0.0.1", 56101, 0x6101, 0x0001));
    
    // 创建测试数据
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.registry_version = "1.0.0";
    snapshot.collected_at = "2026-07-23T22:30:00Z";
    snapshot.snapshot_seq = 1;
    
    // 添加一些 ECU 数据
    EcuVersionEntry ecu1;
    ecu1.ecu_id = "ECU001";
    ecu1.part_number = "PN001";
    ecu1.sw_version = "1.0.0";
    ecu1.hw_version = "HW1.0";
    snapshot.ecu_list.push_back(ecu1);
    
    EcuVersionEntry ecu2;
    ecu2.ecu_id = "ECU002";
    ecu2.part_number = "PN002";
    ecu2.sw_version = "2.0.0";
    ecu2.hw_version = "HW2.0";
    snapshot.ecu_list.push_back(ecu2);
    
    // 上报软件清单
    bool result = testable_client->reportSoftwareInventory(snapshot);
    EXPECT_TRUE(result);
    EXPECT_EQ(testable_client->getLastReportedVin(), "12345678901234567");
    EXPECT_EQ(testable_client->getLastReportedSeq(), 1u);
}

TEST_F(TboxSomeipIntegrationTest, ReportSoftwareInventoryWithRetry) {
    // 使用 TestableSomeIpTboxClient 避免真实 TCP 连接
    auto testable_client = std::make_shared<TestableSomeIpTboxClient>();
    ASSERT_TRUE(testable_client->connect("127.0.0.1", 56101, 0x6101, 0x0001));
    
    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.registry_version = "1.0.0";
    snapshot.collected_at = "2026-07-23T22:30:00Z";
    snapshot.snapshot_seq = 2;
    
    // 上报软件清单（带重试）
    bool result = testable_client->reportSoftwareInventoryWithRetry(snapshot, 3, 100);
    EXPECT_TRUE(result);
}

TEST_F(TboxSomeipIntegrationTest, DisconnectAndReconnect) {
    // 测试断开连接和重新连接（真实 TCP）
    ASSERT_TRUE(client->connect("127.0.0.1", 56101, 0x6101, 0x0001));
    EXPECT_TRUE(client->isConnected());
    
    client->disconnect();
    EXPECT_FALSE(client->isConnected());
    
    // 重新连接
    ASSERT_TRUE(client->connect("127.0.0.1", 56101, 0x6101, 0x0001));
    EXPECT_TRUE(client->isConnected());
}

TEST_F(TboxSomeipIntegrationTest, MultipleReports) {
    // 使用 TestableSomeIpTboxClient 避免真实 TCP 连接
    auto testable_client = std::make_shared<TestableSomeIpTboxClient>();
    ASSERT_TRUE(testable_client->connect("127.0.0.1", 56101, 0x6101, 0x0001));
    
    for (int i = 0; i < 5; ++i) {
        VehicleSoftwareSnapshot snapshot;
        snapshot.vin = "12345678901234567";
        snapshot.registry_version = "1.0.0";
        snapshot.collected_at = "2026-07-23T22:30:00Z";
        snapshot.snapshot_seq = i + 100;
        
        EcuVersionEntry ecu;
        ecu.ecu_id = "ECU" + std::to_string(i);
        ecu.sw_version = "1.0." + std::to_string(i);
        snapshot.ecu_list.push_back(ecu);
        
        bool result = testable_client->reportSoftwareInventory(snapshot);
        EXPECT_TRUE(result);
    }
    
    // Verify via last reported values instead of count (polymorphic call)
    EXPECT_EQ(testable_client->getLastReportedVin(), "12345678901234567");
    EXPECT_EQ(testable_client->getLastReportedSeq(), 104u);
}

TEST_F(TboxSomeipIntegrationTest, InvalidConnection) {
    // connect() now does real TCP - should fail to connect to invalid port
    bool connected = client->connect("127.0.0.1", 65535, 0x6101, 0x0001);
    EXPECT_FALSE(connected);
}
