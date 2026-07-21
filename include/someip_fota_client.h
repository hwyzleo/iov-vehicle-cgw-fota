#pragma once

#include "data_models.h"
#include <string>
#include <vector>
#include <memory>

namespace cgw_fota {

class SomeIpFotaClient {
public:
    SomeIpFotaClient();
    virtual ~SomeIpFotaClient();

    virtual bool connect(const std::string& ip_address, uint16_t port);
    virtual bool disconnect();
    virtual bool isConnected() const;

    virtual bool collectVehicleInventory(const std::string& vin, VehicleSoftwareSnapshot& snapshot);
    virtual bool getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry);
    virtual bool getRegistryVersion(std::string& version);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace cgw_fota
