#include "someip_tbox_client.h"
#include "error_codes.h"
#include <iostream>
#include <thread>
#include <chrono>

namespace cgw_fota {

class SomeIpTboxClient::Impl {
public:
    bool connected = false;
    std::string ip_address;
    uint16_t port = 0;

    bool connect(const std::string& ip, uint16_t p) {
        ip_address = ip;
        port = p;
        connected = true;
        return true;
    }

    bool disconnect() {
        connected = false;
        return true;
    }

    bool isConnected() const {
        return connected;
    }

    bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
        if (!connected) {
            return false;
        }

        // Mock implementation - in real implementation, this would call SOME/IP service
        std::cout << "Reporting software inventory for VIN: " << snapshot.vin << std::endl;
        std::cout << "Snapshot sequence: " << snapshot.snapshot_seq << std::endl;
        std::cout << "ECU count: " << snapshot.ecu_list.size() << std::endl;

        return true;
    }

    bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                         uint32_t max_retries,
                                         uint32_t retry_interval_ms) {
        if (!connected) {
            return false;
        }

        for (uint32_t attempt = 0; attempt <= max_retries; ++attempt) {
            if (reportSoftwareInventory(snapshot)) {
                return true;
            }

            if (attempt < max_retries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms));
            }
        }

        return false;
    }
};

SomeIpTboxClient::SomeIpTboxClient() : pimpl_(std::make_unique<Impl>()) {}

SomeIpTboxClient::~SomeIpTboxClient() = default;

bool SomeIpTboxClient::connect(const std::string& ip_address, uint16_t port) {
    return pimpl_->connect(ip_address, port);
}

bool SomeIpTboxClient::disconnect() {
    return pimpl_->disconnect();
}

bool SomeIpTboxClient::isConnected() const {
    return pimpl_->isConnected();
}

bool SomeIpTboxClient::reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) {
    return pimpl_->reportSoftwareInventory(snapshot);
}

bool SomeIpTboxClient::reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                                       uint32_t max_retries,
                                                       uint32_t retry_interval_ms) {
    return pimpl_->reportSoftwareInventoryWithRetry(snapshot, max_retries, retry_interval_ms);
}

} // namespace cgw_fota
