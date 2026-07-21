#include "someip_fota_client.h"
#include "error_codes.h"
#include <iostream>
#include <sstream>

namespace cgw_fota {

class SomeIpFotaClient::Impl {
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

    bool collectVehicleInventory(const std::string& vin, VehicleSoftwareSnapshot& snapshot) {
        if (!connected) {
            return false;
        }

        snapshot.vin = vin;
        snapshot.baseline_id = "BASELINE001";
        snapshot.baseline_source = BaselineSource::FACTORY;
        snapshot.registry_version = "1.0.0";
        snapshot.collected_at = "2026-07-21T10:00:00Z";
        snapshot.overall_result = CollectionStatus::ALL_OK;
        snapshot.snapshot_seq = 1;

        EcuVersionEntry entry1;
        entry1.ecu_id = "ECU001";
        entry1.part_number = "PN001";
        entry1.sw_version = "1.0.0";
        entry1.hw_version = "HW1.0";
        entry1.source = VersionSource::UDS_0x22;
        entry1.status = EcuStatus::OK;

        EcuVersionEntry entry2;
        entry2.ecu_id = "ECU002";
        entry2.part_number = "PN002";
        entry2.sw_version = "2.0.0";
        entry2.hw_version = "HW2.0";
        entry2.source = VersionSource::SOMEIP_GET_VERSION;
        entry2.status = EcuStatus::OK;

        snapshot.ecu_list = {entry1, entry2};

        return true;
    }

    bool getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry) {
        if (!connected) {
            return false;
        }

        entry.ecu_id = ecu_id;
        entry.part_number = "PN" + ecu_id.substr(3);
        entry.sw_version = "1.0.0";
        entry.hw_version = "HW1.0";
        entry.source = VersionSource::UDS_0x22;
        entry.status = EcuStatus::OK;

        return true;
    }

    bool getRegistryVersion(std::string& version) {
        if (!connected) {
            return false;
        }

        version = "1.0.0";
        return true;
    }
};

SomeIpFotaClient::SomeIpFotaClient() : pimpl_(std::make_unique<Impl>()) {}

SomeIpFotaClient::~SomeIpFotaClient() = default;

bool SomeIpFotaClient::connect(const std::string& ip_address, uint16_t port) {
    return pimpl_->connect(ip_address, port);
}

bool SomeIpFotaClient::disconnect() {
    return pimpl_->disconnect();
}

bool SomeIpFotaClient::isConnected() const {
    return pimpl_->isConnected();
}

bool SomeIpFotaClient::collectVehicleInventory(const std::string& vin, VehicleSoftwareSnapshot& snapshot) {
    return pimpl_->collectVehicleInventory(vin, snapshot);
}

bool SomeIpFotaClient::getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry) {
    return pimpl_->getEcuVersion(ecu_id, entry);
}

bool SomeIpFotaClient::getRegistryVersion(std::string& version) {
    return pimpl_->getRegistryVersion(version);
}

} // namespace cgw_fota
