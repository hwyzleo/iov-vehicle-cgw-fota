#pragma once

#include "data_models.h"
#include "someip_fota_client.h"
#include <memory>
#include <chrono>
#include <mutex>

namespace cgw_fota {

namespace store { class FotaStateStore; }  // CGW-FOTA-DSN-CR-005

class SnapshotAssembler {
public:
    SnapshotAssembler(std::shared_ptr<SomeIpFotaClient> client);
    virtual ~SnapshotAssembler() = default;

    virtual bool assembleSnapshot(VehicleSoftwareSnapshot& snapshot);

    void setThrottleInterval(uint32_t interval_ms);
    void setMaxEcuCount(uint32_t max_count);

    // 注入状态存储以启用 durable 序号分配 (CGW-FOTA-DSN-CR-005)。
    // 未注入时回退到内存序号（仅用于测试/mock）。
    void setStateStore(std::shared_ptr<store::FotaStateStore> store);

private:
    std::shared_ptr<SomeIpFotaClient> client_;
    std::shared_ptr<store::FotaStateStore> state_store_;
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
