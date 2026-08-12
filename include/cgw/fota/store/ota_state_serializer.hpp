#pragma once

// =============================================================================
// include/cgw/fota/store/ota_state_serializer.hpp
// CGW-FOTA OTA 状态序列化器 (CGW-FOTA-DSN-CR-009 §13.3)
// =============================================================================
// 将 ota_state.hpp 中的记录序列化为版本化 FOTA envelope（复用既有 FTSE 帧头 +
// JSON payload），供 cgw-framework-store 以 std::string 持久化。复杂 proto 消息
// 以 proto-binary hex 内嵌。未知新版本 fail-closed。
// =============================================================================

#include "cgw/fota/store/ota_state.hpp"

#include <string>

namespace cgw_fota {
namespace store {
namespace ota {

// proto-binary <-> hex 辅助（供 orchestrator 在 record 字段与 proto 消息间转换）。
std::string protoBinaryToHex(const std::string& binary);
std::string hexToProtoBinary(const std::string& hex);

// 每类记录的 encode（record -> envelope string）
std::string encodeVehicleTask(const OtaVehicleTaskRecord& r);
std::string encodeInventory(const OtaInventoryRecord& r);
std::string encodeConsent(const OtaConsentRecord& r);
std::string encodeDownloads(const OtaDownloadsRecord& r);
std::string encodeExecution(const OtaExecutionRecord& r);
std::string encodeEventOutbox(const OtaEventOutboxRecord& r);
std::string encodeControls(const OtaControlsRecord& r);
std::string encodePolicy(const OtaPolicyRecord& r);
std::string encodeLogJobs(const OtaLogJobsRecord& r);

// 每类记录的 decode（envelope string -> record），含 schemaVersion 校验
OtaVehicleTaskRecord decodeVehicleTask(const std::string& bytes);
OtaInventoryRecord    decodeInventory(const std::string& bytes);
OtaConsentRecord      decodeConsent(const std::string& bytes);
OtaDownloadsRecord    decodeDownloads(const std::string& bytes);
OtaExecutionRecord    decodeExecution(const std::string& bytes);
OtaEventOutboxRecord  decodeEventOutbox(const std::string& bytes);
OtaControlsRecord     decodeControls(const std::string& bytes);
OtaPolicyRecord       decodePolicy(const std::string& bytes);
OtaLogJobsRecord      decodeLogJobs(const std::string& bytes);

} // namespace ota
} // namespace store
} // namespace cgw_fota
