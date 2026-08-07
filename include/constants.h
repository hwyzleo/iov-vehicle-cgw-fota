#pragma once

#include <cstdint>
#include <string>

namespace cgw_fota {

// Default configuration values
constexpr uint32_t DEFAULT_MAX_ECU_COUNT = 100;
constexpr uint64_t DEFAULT_SNAPSHOT_SEQ_INITIAL = 1;
constexpr uint32_t DEFAULT_THROTTLE_INTERVAL_MS = 5000;
constexpr uint32_t DEFAULT_DEDUP_WINDOW_SIZE = 100;
constexpr uint32_t DEFAULT_INITIAL_REPORT_DELAY_MS = 1000;
constexpr uint32_t DEFAULT_MAX_RETRY_COUNT = 3;
constexpr uint32_t DEFAULT_RETRY_INTERVAL_MS = 1000;

// ============================================================
// SOME/IP 寻址（CGW-FOTA-DSN-CR-004）
// 过渡 SSOT：在整车 SOME/IP Service Registry 落地前，寻址值由本常量供给；
// fota.yaml 不再承载 Service/Instance/Method ID、协议或端口。
// ============================================================
// CGW-DIAG service (CGW-DIAG-DSN-CR-007: 0x1110 / 0x0001 / TCP / 51110)
constexpr uint16_t DEFAULT_DIAG_SERVICE_ID  = 0x1110;
constexpr uint16_t DEFAULT_DIAG_INSTANCE_ID = 0x0001;
constexpr uint16_t DEFAULT_DIAG_PORT        = 51110;
const std::string  DEFAULT_DIAG_IP_ADDRESS  = "127.0.0.1";

// TBOX-SOMEIP service (CGW-FOTA-DSN-CR-002: 0x6101 / 0x0001 / TCP / 56101)
constexpr uint16_t DEFAULT_TBOX_SERVICE_ID  = 0x6101;
constexpr uint16_t DEFAULT_TBOX_INSTANCE_ID = 0x0001;
constexpr uint16_t DEFAULT_TBOX_PORT        = 56101;
const std::string  DEFAULT_TBOX_IP_ADDRESS  = "127.0.0.1";

// FOTA Provider service (CGW-FOTA-DSN-CR-002: 0x1120 / 0x0001 / TCP / 51120)
constexpr uint16_t FOTA_PROVIDER_SERVICE_ID  = 0x1120;
constexpr uint16_t FOTA_PROVIDER_INSTANCE_ID = 0x0001;
constexpr uint16_t FOTA_PROVIDER_PORT        = 51120;
const std::string  FOTA_PROVIDER_IP_ADDRESS  = "0.0.0.0";

// FOTA Provider Method IDs (service-scoped)
constexpr uint16_t METHOD_REQUEST_SOFTWARE_INVENTORY = 0x0001;

// TBOX-SOMEIP Method IDs (service-scoped)
// Note: CGW-FOTA proactively reports software inventory to TBOX
constexpr uint16_t TBOX_METHOD_REPORT_SOFTWARE_INVENTORY = 0x0001;

// Method IDs for CGW-DIAG (DIAG service-scoped)
// Note: Cloud requests vehicle to collect and report
constexpr uint16_t METHOD_READ_VIN = 0x0001;
constexpr uint16_t METHOD_COLLECT_VEHICLE_INVENTORY = 0x0002;
constexpr uint16_t METHOD_GET_ECU_VERSION = 0x0003;
constexpr uint16_t METHOD_GET_REGISTRY_VERSION = 0x0004;
constexpr uint16_t METHOD_REPORT_SOFTWARE_INVENTORY = 0x0005;

// Event IDs for SOME/IP
constexpr uint16_t EVENT_VERSION_CHANGED = 0x0001;

// MQTT topic
const std::string MQTT_TOPIC_PREFIX = "vehicle/";
const std::string MQTT_TOPIC_SUFFIX = "/up/fota";

} // namespace cgw_fota
