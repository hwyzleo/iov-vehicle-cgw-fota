#include "snapshot_assembler.h"
#include "error_codes.h"
#include <iostream>

namespace cgw_fota {

SnapshotAssembler::SnapshotAssembler(std::shared_ptr<SomeIpFotaClient> client)
    : client_(client)
    , current_seq_(1)
    , throttle_interval_ms_(5000) // Default 5 seconds
    , max_ecu_count_(100) // Default 100 ECUs
    , last_report_time_(std::chrono::steady_clock::now() - std::chrono::hours(1)) // Allow immediate first report
{
}

bool SnapshotAssembler::assembleSnapshot(VehicleSoftwareSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check throttling
    if (isThrottled()) {
        std::cout << "Reporting throttled, skipping" << std::endl;
        return false;
    }

    // Collect vehicle inventory from CGW-DIAG (VIN comes from DIAG)
    if (!client_->collectVehicleInventory(snapshot)) {
        std::cout << "Failed to collect vehicle inventory" << std::endl;
        return false;
    }

    // Validate snapshot
    if (!validateSnapshot(snapshot)) {
        std::cout << "Snapshot validation failed" << std::endl;
        return false;
    }

    // Update sequence number
    snapshot.snapshot_seq = current_seq_++;

    // Update last report time
    updateLastReportTime();

    return true;
}

void SnapshotAssembler::setThrottleInterval(uint32_t interval_ms) {
    throttle_interval_ms_ = interval_ms;
}

void SnapshotAssembler::setMaxEcuCount(uint32_t max_count) {
    max_ecu_count_ = max_count;
}

bool SnapshotAssembler::isThrottled() {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_report_time_);
    return elapsed.count() < throttle_interval_ms_;
}

void SnapshotAssembler::updateLastReportTime() {
    last_report_time_ = std::chrono::steady_clock::now();
}

bool SnapshotAssembler::validateSnapshot(const VehicleSoftwareSnapshot& snapshot) {
    // Check VIN is not empty
    if (snapshot.vin.empty()) {
        return false;
    }

    // Check ECU count doesn't exceed maximum
    if (snapshot.ecu_list.size() > max_ecu_count_) {
        return false;
    }

    // Check overall result is valid
    if (snapshot.overall_result != CollectionStatus::ALL_OK &&
        snapshot.overall_result != CollectionStatus::PARTIAL &&
        snapshot.overall_result != CollectionStatus::FAILED) {
        return false;
    }

    return true;
}

} // namespace cgw_fota
