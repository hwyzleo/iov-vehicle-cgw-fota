#pragma once

#include "data_models.h"
#include <string>
#include <memory>

namespace cgw_fota {

class SomeIpTboxClient {
public:
    SomeIpTboxClient();
    virtual ~SomeIpTboxClient();

    virtual bool connect(const std::string& ip_address, uint16_t port);
    virtual bool disconnect();
    virtual bool isConnected() const;

    virtual bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot);
    virtual bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                                 uint32_t max_retries,
                                                 uint32_t retry_interval_ms);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
};

} // namespace cgw_fota
