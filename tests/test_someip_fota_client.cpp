#include "someip_fota_client.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace cgw_fota;
using ::testing::_;
using ::testing::Return;

// Testable subclass that can provide VIN from "DIAG"
class TestableSomeIpFotaClient : public SomeIpFotaClient {
public:
    bool getVin(std::string& vin) override {
        if (!isConnected()) {
            return false;
        }
        vin = test_vin_;
        return true;
    }

    void setTestVin(const std::string& vin) {
        test_vin_ = vin;
    }

private:
    std::string test_vin_ = "12345678901234567";
};

class MockSomeIpTransport {
public:
    MOCK_METHOD(bool, connect, (const std::string& ip, uint16_t port));
    MOCK_METHOD(bool, disconnect, ());
    MOCK_METHOD(bool, sendRequest, (uint16_t service_id, uint16_t method_id, const std::vector<uint8_t>& request, std::vector<uint8_t>& response));
};

class SomeIpFotaClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(SomeIpFotaClientTest, ConnectToDiagService) {
    SomeIpFotaClient client;
    bool result = client.connect("127.0.0.1", 30501);

    EXPECT_TRUE(result);
    EXPECT_TRUE(client.isConnected());
}

TEST_F(SomeIpFotaClientTest, DisconnectFromDiagService) {
    SomeIpFotaClient client;
    client.connect("127.0.0.1", 30501);

    bool result = client.disconnect();

    EXPECT_TRUE(result);
    EXPECT_FALSE(client.isConnected());
}

TEST_F(SomeIpFotaClientTest, CollectVehicleInventory) {
    TestableSomeIpFotaClient client;
    client.setTestVin("12345678901234567");
    client.connect("127.0.0.1", 30501);

    VehicleSoftwareSnapshot snapshot;
    bool result = client.collectVehicleInventory(snapshot);

    EXPECT_TRUE(result);
    EXPECT_EQ(snapshot.vin, "12345678901234567");
    EXPECT_FALSE(snapshot.ecu_list.empty());
}

TEST_F(SomeIpFotaClientTest, GetEcuVersion) {
    SomeIpFotaClient client;
    client.connect("127.0.0.1", 30501);

    EcuVersionEntry entry;
    bool result = client.getEcuVersion("ECU001", entry);

    EXPECT_TRUE(result);
    EXPECT_EQ(entry.ecu_id, "ECU001");
}

TEST_F(SomeIpFotaClientTest, GetRegistryVersion) {
    SomeIpFotaClient client;
    client.connect("127.0.0.1", 30501);

    std::string version;
    bool result = client.getRegistryVersion(version);

    EXPECT_TRUE(result);
    EXPECT_FALSE(version.empty());
}

TEST_F(SomeIpFotaClientTest, NotConnectedError) {
    SomeIpFotaClient client;
    // Don't connect

    VehicleSoftwareSnapshot snapshot;
    bool result = client.collectVehicleInventory(snapshot);

    EXPECT_FALSE(result);
}
