#pragma once

#include "data_models.h"
#include <string>
#include <vector>
#include <memory>

namespace cgw_fota {

class SomeIpFotaClient {
public:
    SomeIpFotaClient();
    ~SomeIpFotaClient();

    bool connect(const std::string& ip_address, uint16_t port);
    bool disconnect();
    bool isConnected() const;

    bool collectVehicleInventory(const std::string& vin, VehicleSoftwareSnapshot& snapshot);
    bool getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry);
    bool getRegistryVersion(std::string& version);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace cgw_fota
