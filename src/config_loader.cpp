#include "config_loader.h"
#include "constants.h"
#include <yaml-cpp/yaml.h>

namespace cgw_fota {

ConfigLoader::ConfigLoader()
    : max_ecu_count_(DEFAULT_MAX_ECU_COUNT)
    , snapshot_seq_initial_(DEFAULT_SNAPSHOT_SEQ_INITIAL)
    , throttle_interval_ms_(DEFAULT_THROTTLE_INTERVAL_MS)
    , dedup_window_size_(DEFAULT_DEDUP_WINDOW_SIZE)
    , diag_service_id_(DEFAULT_DIAG_SERVICE_ID)
    , diag_instance_id_(DEFAULT_DIAG_INSTANCE_ID)
    , diag_ip_address_("127.0.0.1")
    , diag_port_(30501)
    , tbox_service_id_(DEFAULT_TBOX_SERVICE_ID)
    , tbox_instance_id_(DEFAULT_TBOX_INSTANCE_ID)
    , tbox_ip_address_("127.0.0.1")
    , tbox_port_(30502)
    , initial_report_delay_ms_(DEFAULT_INITIAL_REPORT_DELAY_MS)
    , max_retry_count_(DEFAULT_MAX_RETRY_COUNT)
    , retry_interval_ms_(DEFAULT_RETRY_INTERVAL_MS)
    , log_level_("INFO")
    , log_file_("/var/log/cgw_fota.log")
{
}

bool ConfigLoader::loadConfig(const std::string& config_path) {
    try {
        YAML::Node config = YAML::LoadFile(config_path);

        if (config["fota"]) {
            auto fota = config["fota"];

            if (fota["snapshot"]) {
                auto snapshot = fota["snapshot"];
                if (snapshot["max_ecu_count"]) {
                    max_ecu_count_ = snapshot["max_ecu_count"].as<uint32_t>();
                }
                if (snapshot["snapshot_seq_initial"]) {
                    snapshot_seq_initial_ = snapshot["snapshot_seq_initial"].as<uint64_t>();
                }
                if (snapshot["throttle_interval_ms"]) {
                    throttle_interval_ms_ = snapshot["throttle_interval_ms"].as<uint32_t>();
                }
                if (snapshot["dedup_window_size"]) {
                    dedup_window_size_ = snapshot["dedup_window_size"].as<uint32_t>();
                }
            }

            if (fota["someip"]) {
                auto someip = fota["someip"];

                if (someip["diag_service"]) {
                    auto diag = someip["diag_service"];
                    if (diag["service_id"]) {
                        diag_service_id_ = diag["service_id"].as<uint16_t>();
                    }
                    if (diag["instance_id"]) {
                        diag_instance_id_ = diag["instance_id"].as<uint16_t>();
                    }
                    if (diag["ip_address"]) {
                        diag_ip_address_ = diag["ip_address"].as<std::string>();
                    }
                    if (diag["port"]) {
                        diag_port_ = diag["port"].as<uint16_t>();
                    }
                }

                if (someip["tbox_service"]) {
                    auto tbox = someip["tbox_service"];
                    if (tbox["service_id"]) {
                        tbox_service_id_ = tbox["service_id"].as<uint16_t>();
                    }
                    if (tbox["instance_id"]) {
                        tbox_instance_id_ = tbox["instance_id"].as<uint16_t>();
                    }
                    if (tbox["ip_address"]) {
                        tbox_ip_address_ = tbox["ip_address"].as<std::string>();
                    }
                    if (tbox["port"]) {
                        tbox_port_ = tbox["port"].as<uint16_t>();
                    }
                }
            }

            if (fota["reporting"]) {
                auto reporting = fota["reporting"];
                if (reporting["initial_report_delay_ms"]) {
                    initial_report_delay_ms_ = reporting["initial_report_delay_ms"].as<uint32_t>();
                }
                if (reporting["max_retry_count"]) {
                    max_retry_count_ = reporting["max_retry_count"].as<uint32_t>();
                }
                if (reporting["retry_interval_ms"]) {
                    retry_interval_ms_ = reporting["retry_interval_ms"].as<uint32_t>();
                }
            }

            if (fota["logging"]) {
                auto logging = fota["logging"];
                if (logging["level"]) {
                    log_level_ = logging["level"].as<std::string>();
                }
                if (logging["file"]) {
                    log_file_ = logging["file"].as<std::string>();
                }
            }
        }

        return true;
    } catch (const std::exception&) {
        return false;
    }
}

uint32_t ConfigLoader::getMaxEcuCount() const {
    return max_ecu_count_;
}

uint64_t ConfigLoader::getSnapshotSeqInitial() const {
    return snapshot_seq_initial_;
}

uint32_t ConfigLoader::getThrottleIntervalMs() const {
    return throttle_interval_ms_;
}

uint32_t ConfigLoader::getDedupWindowSize() const {
    return dedup_window_size_;
}

uint16_t ConfigLoader::getDiagServiceId() const {
    return diag_service_id_;
}

uint16_t ConfigLoader::getDiagInstanceId() const {
    return diag_instance_id_;
}

std::string ConfigLoader::getDiagIpAddress() const {
    return diag_ip_address_;
}

uint16_t ConfigLoader::getDiagPort() const {
    return diag_port_;
}

uint16_t ConfigLoader::getTboxServiceId() const {
    return tbox_service_id_;
}

uint16_t ConfigLoader::getTboxInstanceId() const {
    return tbox_instance_id_;
}

std::string ConfigLoader::getTboxIpAddress() const {
    return tbox_ip_address_;
}

uint16_t ConfigLoader::getTboxPort() const {
    return tbox_port_;
}

uint32_t ConfigLoader::getInitialReportDelayMs() const {
    return initial_report_delay_ms_;
}

uint32_t ConfigLoader::getMaxRetryCount() const {
    return max_retry_count_;
}

uint32_t ConfigLoader::getRetryIntervalMs() const {
    return retry_interval_ms_;
}

std::string ConfigLoader::getLogLevel() const {
    return log_level_;
}

std::string ConfigLoader::getLogFile() const {
    return log_file_;
}

} // namespace cgw_fota
