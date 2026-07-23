#pragma once

#include "data_models.h"
#include <string>
#include <memory>

namespace cgw_fota {

class SomeIpTboxClient {
public:
    SomeIpTboxClient();
    virtual ~SomeIpTboxClient();

    // 连接到 TBOX-SOMEIP 服务
    // ip_address: 服务 IP 地址
    // port: 服务端口（默认 56101）
    // service_id: 服务 ID（默认 0x6101）
    // instance_id: 实例 ID（默认 0x0001）
    virtual bool connect(const std::string& ip_address, uint16_t port,
                        uint16_t service_id = 0x6101, uint16_t instance_id = 0x0001);
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
