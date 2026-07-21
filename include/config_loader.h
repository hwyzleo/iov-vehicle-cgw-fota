#pragma once

#include <string>
#include <cstdint>

namespace cgw_fota {

class ConfigLoader {
public:
    ConfigLoader();
    ~ConfigLoader() = default;

    bool loadConfig(const std::string& config_path);

    // Snapshot configuration
    uint32_t getMaxEcuCount() const;
    uint64_t getSnapshotSeqInitial() const;
    uint32_t getThrottleIntervalMs() const;
    uint32_t getDedupWindowSize() const;

    // SOME/IP configuration
    uint16_t getDiagServiceId() const;
    uint16_t getDiagInstanceId() const;
    std::string getDiagIpAddress() const;
    uint16_t getDiagPort() const;

    uint16_t getTboxServiceId() const;
    uint16_t getTboxInstanceId() const;
    std::string getTboxIpAddress() const;
    uint16_t getTboxPort() const;

    // Reporting configuration
    uint32_t getInitialReportDelayMs() const;
    uint32_t getMaxRetryCount() const;
    uint32_t getRetryIntervalMs() const;

    // Logging configuration
    std::string getLogLevel() const;
    std::string getLogFile() const;

private:
    // Snapshot configuration
    uint32_t max_ecu_count_;
    uint64_t snapshot_seq_initial_;
    uint32_t throttle_interval_ms_;
    uint32_t dedup_window_size_;

    // SOME/IP configuration
    uint16_t diag_service_id_;
    uint16_t diag_instance_id_;
    std::string diag_ip_address_;
    uint16_t diag_port_;

    uint16_t tbox_service_id_;
    uint16_t tbox_instance_id_;
    std::string tbox_ip_address_;
    uint16_t tbox_port_;

    // Reporting configuration
    uint32_t initial_report_delay_ms_;
    uint32_t max_retry_count_;
    uint32_t retry_interval_ms_;

    // Logging configuration
    std::string log_level_;
    std::string log_file_;
};

} // namespace cgw_fota
