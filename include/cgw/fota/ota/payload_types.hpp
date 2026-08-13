#pragma once

// =============================================================================
// include/cgw/fota/ota/payload_types.hpp
// CGW-FOTA 通用传输 payloadType 常量 (CGW-FOTA-DSN-CR-010 §Envelope 设计)
// =============================================================================
// payloadType 是稳定、版本化的业务消息类型标识（如 "ota.task-check.request.v1"），
// 表达具体业务消息类型和 major；TBOX 可用于 allowlist/路由，但不得解析 payload
// 字段。新增向后兼容业务消息只增加新的 payloadType，不修改 VehicleMessageTransport
// C++ API，也不新增 SOME/IP Method。
//
// 命名规则：ota.<operation>.<kind>.<major>。kind 取值 request/response/report/event。
// =============================================================================

#include <string_view>

namespace cgw_fota {
namespace ota {
namespace payload_type {

// 稳定业务域标识与协议版本。
inline constexpr std::string_view kService = "vehicle.ota";
inline constexpr std::string_view kProtocolVersion = "ota-v1";

// 1. 任务检测
inline constexpr std::string_view kTaskCheckRequest  = "ota.task-check.request.v1";
inline constexpr std::string_view kTaskCheckResponse = "ota.task-check.response.v1";

// 2. 授权结果上报
inline constexpr std::string_view kConsentReport   = "ota.consent.report.v1";
inline constexpr std::string_view kConsentResponse = "ota.consent.response.v1";

// 3. 下载授权
inline constexpr std::string_view kDownloadGrantRequest  = "ota.download-grant.request.v1";
inline constexpr std::string_view kDownloadGrantResponse = "ota.download-grant.response.v1";

// 4. 下载/校验阶段结果
inline constexpr std::string_view kStageResultReport   = "ota.stage-result.report.v1";
inline constexpr std::string_view kStageResultResponse = "ota.stage-result.response.v1";

// 5. 安装许可
inline constexpr std::string_view kInstallPermitRequest  = "ota.install-permit.request.v1";
inline constexpr std::string_view kInstallPermitResponse = "ota.install-permit.response.v1";

// 6. 执行事件
inline constexpr std::string_view kExecutionEvent   = "ota.execution-event.v1";
inline constexpr std::string_view kEventResponse    = "ota.event-response.v1";

// 7. 控制回执
inline constexpr std::string_view kControlAck        = "ota.control-ack.v1";
inline constexpr std::string_view kControlAckResponse = "ota.control-ack.response.v1";

// 8. 最终结果
inline constexpr std::string_view kFinalResult       = "ota.final-result.v1";
inline constexpr std::string_view kFinalResultResponse = "ota.final-result.response.v1";

// 9. 日志上传凭证
inline constexpr std::string_view kLogGrantRequest  = "ota.log-grant.request.v1";
inline constexpr std::string_view kLogGrantResponse = "ota.log-grant.response.v1";

// 10. 日志上传结果
inline constexpr std::string_view kLogUploadResult = "ota.log-upload-result.v1";
inline constexpr std::string_view kLogResultResponse = "ota.log-result.response.v1";

// 11. 对账
inline constexpr std::string_view kReconcileRequest  = "ota.reconcile.request.v1";
inline constexpr std::string_view kReconcileResponse = "ota.reconcile.response.v1";

// 12. 策略同步
inline constexpr std::string_view kPolicyRequest  = "ota.policy.request.v1";
inline constexpr std::string_view kPolicyResponse = "ota.policy.response.v1";

// 下行：云端控制指令（事件，经 subscribe 进入）。
inline constexpr std::string_view kControlCommand = "ota.control.command.v1";

} // namespace payload_type
} // namespace ota
} // namespace cgw_fota
