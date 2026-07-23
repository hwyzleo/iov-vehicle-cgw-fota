#pragma once

#include "data_models.h"
#include "someip_fota_client.h"
#include <memory>
#include <chrono>
#include <mutex>

namespace cgw_fota {

class SnapshotAssembler {
public:
    SnapshotAssembler(std::shared_ptr<SomeIpFotaClient> client);
    virtual ~SnapshotAssembler() = default;

    virtual bool assembleSnapshot(VehicleSoftwareSnapshot& snapshot);

    void setThrottleInterval(uint32_t interval_ms);
    void setMaxEcuCount(uint32_t max_count);

private:
    std::shared_ptr<SomeIpFotaClient> client_;
    uint64_t current_seq_;
    uint32_t throttle_interval_ms_;
    uint32_t max_ecu_count_;
    std::chrono::steady_clock::time_point last_report_time_;
    std::mutex mutex_;

    bool isThrottled();
    void updateLastReportTime();
    bool validateSnapshot(const VehicleSoftwareSnapshot& snapshot);
};

} // namespace cgw_fota
