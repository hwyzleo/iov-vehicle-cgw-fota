#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace cgw_fota {

enum class VersionSource {
    UDS_0x22,
    SOMEIP_GET_VERSION
};

enum class CollectionStatus {
    ALL_OK,
    PARTIAL,
    FAILED
};

enum class EcuStatus {
    OK,
    MISSING,
    NRC,
    TIMEOUT,
    UNREACHABLE,
    PARSE_ERROR
};

enum class BaselineSource {
    FACTORY,
    LAST_OTA,
    UNKNOWN
};

struct EcuVersionEntry {
    std::string ecu_id;
    std::optional<std::string> part_number;
    std::optional<std::string> sw_version;
    std::optional<std::string> hw_version;
    VersionSource source = VersionSource::UDS_0x22;
    EcuStatus status = EcuStatus::OK;
    std::optional<std::string> error_code;
};

struct VehicleSoftwareSnapshot {
    std::string vin;
    std::optional<std::string> baseline_id;
    BaselineSource baseline_source = BaselineSource::UNKNOWN;
    std::string registry_version;
    std::string collected_at;
    CollectionStatus overall_result = CollectionStatus::ALL_OK;
    uint64_t snapshot_seq = 0;
    std::vector<EcuVersionEntry> ecu_list;
};

} // namespace cgw_fota
