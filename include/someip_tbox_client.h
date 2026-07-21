#pragma once

#include "data_models.h"
#include <string>
#include <memory>

namespace cgw_fota {

class SomeIpTboxClient {
public:
    SomeIpTboxClient();
    ~SomeIpTboxClient();

    bool connect(const std::string& ip_address, uint16_t port);
    bool disconnect();
    bool isConnected() const;

    bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot);
    bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                         uint32_t max_retries,
                                         uint32_t retry_interval_ms);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace cgw_fota
