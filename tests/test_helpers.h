#pragma once

#include "someip_fota_client.h"
#include "someip_tbox_client.h"
#include "data_models.h"

namespace cgw_fota {
namespace test {

// Testable subclass that can provide VIN from "DIAG"
// Overrides connect/isConnected to avoid real TCP connections in unit tests
class TestableSomeIpFotaClient : public SomeIpFotaClient {
public:
    bool connect(const std::string& ip_address, uint16_t port) override {
        test_connected_ = true;
        return true;
    }

    bool disconnect() override {
        test_connected_ = false;
        return true;
    }

    bool isConnected() const override {
        return test_connected_;
    }

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
    bool test_connected_ = false;
};

// Testable subclass that overrides reportSoftwareInventory to avoid real TCP
// Overrides connect/isConnected to avoid real TCP connections in unit tests
class TestableSomeIpTboxClient : public SomeIpTboxClient {
public:
    bool connect(const std::string& ip_address, uint16_t port,
                 uint16_t service_id = 0x6101, uint16_t instance_id = 0x0001) override {
        test_connected_ = true;
        return true;
    }

    bool disconnect() override {
        test_connected_ = false;
        return true;
    }

    bool isConnected() const override {
        return test_connected_;
    }

    bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) override {
        if (!isConnected()) {
            return false;
        }
        last_reported_vin_ = snapshot.vin;
        last_reported_seq_ = snapshot.snapshot_seq;
        report_count_++;
        return true;
    }

    bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                          uint32_t max_retries,
                                          uint32_t retry_interval_ms) override {
        if (!isConnected()) {
            return false;
        }
        return reportSoftwareInventory(snapshot);
    }

    const std::string& getLastReportedVin() const { return last_reported_vin_; }
    uint64_t getLastReportedSeq() const { return last_reported_seq_; }
    int getReportCount() const { return report_count_; }

private:
    std::string last_reported_vin_;
    uint64_t last_reported_seq_ = 0;
    int report_count_ = 0;
    bool test_connected_ = false;
};

} // namespace test
} // namespace cgw_fota
