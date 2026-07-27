#include "snapshot_assembler.h"
#include "error_codes.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include "constants.h"
#include <chrono>

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

    auto start_time = std::chrono::steady_clock::now();

    // Check throttling
    if (isThrottled()) {
        FotaLogAdapter::snapshot_assembler().warn(
            "fota.snapshot.throttled",
            "Reporting throttled, skipping"
        );
        return false;
    }

    // Create child scope for DIAG call (Service 0x1110)
    auto diag_scope = make_someip_child_scope(
        hex_id(DEFAULT_DIAG_SERVICE_ID),
        hex_id(METHOD_COLLECT_VEHICLE_INVENTORY)
    );

    // Collect vehicle inventory from CGW-DIAG (VIN comes from DIAG)
    if (!client_->collectVehicleInventory(snapshot)) {
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        FotaLogAdapter::snapshot_assembler().error(
            fota_events::DIAG_COLLECT_FAILED,
            "Failed to collect vehicle inventory from DIAG",
            {flog::f_str("report_id", current_report_id()),
             flog::f_str("someip_service_id", hex_id(DEFAULT_DIAG_SERVICE_ID)),
             flog::f_str("someip_method_id", hex_id(METHOD_COLLECT_VEHICLE_INVENTORY)),
             flog::f_int("duration_ms", duration_ms),
             flog::f_str("error_code", "CGW-FOTA-1006")}
        );
        return false;
    }

    auto collect_duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();
    FotaLogAdapter::snapshot_assembler().info(
        fota_events::DIAG_COLLECT_SUCCEEDED,
        "DIAG returned version inventory",
        {flog::f_str("report_id", current_report_id()),
         flog::f_str("registry_version", snapshot.registry_version),
         flog::f_str("overall_result", collectionStatusToString(snapshot.overall_result)),
         flog::f_int("duration_ms", collect_duration_ms)}
    );

    // Validate snapshot
    if (!validateSnapshot(snapshot)) {
        FotaLogAdapter::snapshot_assembler().error(
            fota_events::SNAPSHOT_ASSEMBLE_FAILED,
            "Snapshot validation failed",
            {flog::f_str("report_id", current_report_id()),
             flog::f_str("error_code", "CGW-FOTA-1003")}
        );
        return false;
    }

    // Update sequence number
    snapshot.snapshot_seq = current_seq_++;

    // Update last report time
    updateLastReportTime();

    FotaLogAdapter::snapshot_assembler().info(
        fota_events::SNAPSHOT_ASSEMBLED,
        "Snapshot assembled successfully",
        {flog::f_str("report_id", current_report_id()),
         flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
         flog::f_str("registry_version", snapshot.registry_version),
         flog::f_str("overall_result", collectionStatusToString(snapshot.overall_result)),
         // Payload: only record count, not full ECU list
         flog::f_int("ecu_count", static_cast<int64_t>(snapshot.ecu_list.size()))}
    );

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
