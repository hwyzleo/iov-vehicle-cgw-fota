// =============================================================================
// src/ota/ota_cloud_proxy_via_transport.cpp
// CGW-FOTA OtaCloudProxy 通用传输适配器实现 (CGW-FOTA-DSN-CR-010 §调用流程)
// =============================================================================
// 将 CR-009 的 12 个强类型 OtaCloudProxy 方法逐一迁移到通用 exchange。业务 payload
// 使用 vehicle.ota.v1 Protobuf 经 OtaMessageCodec 编解码；TransportOutcome 映射为
// OtaCloudException（保留 CGW-FW-03xx 链）。本文件不推进状态机、不记录敏感内容。
// =============================================================================

#include "cgw/fota/ota/ota_cloud_proxy_via_transport.hpp"

namespace cgw_fota {
namespace ota {

using ::vehicle::ota::v1::ConsentReport;
using ::vehicle::ota::v1::ConsentResponse;
using ::vehicle::ota::v1::ControlAck;
using ::vehicle::ota::v1::ControlAckResponse;
using ::vehicle::ota::v1::DownloadGrantRequest;
using ::vehicle::ota::v1::DownloadGrantResponse;
using ::vehicle::ota::v1::EventResponse;
using ::vehicle::ota::v1::ExecutionEvent;
using ::vehicle::ota::v1::FinalResult;
using ::vehicle::ota::v1::FinalResultResponse;
using ::vehicle::ota::v1::InstallPermitRequest;
using ::vehicle::ota::v1::InstallPermitResponse;
using ::vehicle::ota::v1::LogGrantRequest;
using ::vehicle::ota::v1::LogGrantResponse;
using ::vehicle::ota::v1::LogResultResponse;
using ::vehicle::ota::v1::LogUploadResult;
using ::vehicle::ota::v1::PolicyRequest;
using ::vehicle::ota::v1::PolicyResponse;
using ::vehicle::ota::v1::ReconcileRequest;
using ::vehicle::ota::v1::ReconcileResponse;
using ::vehicle::ota::v1::StageResultReport;
using ::vehicle::ota::v1::StageResultResponse;
using ::vehicle::ota::v1::TaskCheckRequest;
using ::vehicle::ota::v1::TaskCheckResponse;

OtaCloudProxyViaTransport::OtaCloudProxyViaTransport(VehicleMessageTransport& transport,
                                                     std::size_t maxResponseBytes)
    : transport_(transport)
    , maxResponseBytes_(maxResponseBytes) {}

ExchangeOptions OtaCloudProxyViaTransport::makeOptions(const CallContext& ctx) const {
    ExchangeOptions opts;
    opts.timeout = ctx.timeout;
    opts.max_response_bytes = maxResponseBytes_;
    return opts;
}

void OtaCloudProxyViaTransport::throwFor(TransportOutcome outcome,
                                         std::string_view action) const {
    using K = OtaCloudException::Kind;
    switch (outcome) {
        case TransportOutcome::Timeout:
            throw OtaCloudException(K::Timeout, "CGW-FW-0305",
                                    std::string(action) + " timeout");
        case TransportOutcome::Unavailable:
            throw OtaCloudException(K::Unavailable, "CGW-FW-0304",
                                    std::string(action) + " unavailable");
        case TransportOutcome::VersionMismatch:
        case TransportOutcome::PayloadTooLarge:
        case TransportOutcome::ProtocolError:
            throw OtaCloudException(K::Codec, "CGW-FW-0306",
                                    std::string(action) + " protocol error");
        case TransportOutcome::Stopping:
            throw OtaCloudException(K::Internal, "CGW-FW-0307",
                                    std::string(action) + " stopping");
        case TransportOutcome::Rejected:
        case TransportOutcome::Unknown:
        default:
            // 超时/unknown/rejected 视为可重试，由 OtaOrchestrator 用原业务身份重试。
            throw OtaCloudException(K::Timeout, "CGW-FW-0305",
                                    std::string(action) + " unknown outcome");
    }
}

Subscription OtaCloudProxyViaTransport::subscribeDownlink(std::string_view service,
                                                          DownlinkHandler handler) {
    return transport_.subscribe(service, std::move(handler));
}

// ---------------------------------------------------------------------------
// 1. 任务检测
// ---------------------------------------------------------------------------
TaskCheckResponse OtaCloudProxyViaTransport::checkTask(const TaskCheckRequest& request,
                                                       const CallContext& ctx) {
    return exchangeCall<TaskCheckRequest, TaskCheckResponse>(
        payload_type::kTaskCheckRequest, payload_type::kTaskCheckResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 2. 授权结果上报
// ---------------------------------------------------------------------------
ConsentResponse OtaCloudProxyViaTransport::reportConsent(const ConsentReport& request,
                                                         const CallContext& ctx) {
    return exchangeCall<ConsentReport, ConsentResponse>(
        payload_type::kConsentReport, payload_type::kConsentResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 3. 下载授权
// ---------------------------------------------------------------------------
DownloadGrantResponse OtaCloudProxyViaTransport::requestDownload(
    const DownloadGrantRequest& request, const CallContext& ctx) {
    return exchangeCall<DownloadGrantRequest, DownloadGrantResponse>(
        payload_type::kDownloadGrantRequest, payload_type::kDownloadGrantResponse,
        request, ctx);
}

// ---------------------------------------------------------------------------
// 4. 下载/校验阶段结果
// ---------------------------------------------------------------------------
StageResultResponse OtaCloudProxyViaTransport::reportStageResult(
    const StageResultReport& request, const CallContext& ctx) {
    return exchangeCall<StageResultReport, StageResultResponse>(
        payload_type::kStageResultReport, payload_type::kStageResultResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 5. 安装许可
// ---------------------------------------------------------------------------
InstallPermitResponse OtaCloudProxyViaTransport::requestInstall(
    const InstallPermitRequest& request, const CallContext& ctx) {
    return exchangeCall<InstallPermitRequest, InstallPermitResponse>(
        payload_type::kInstallPermitRequest, payload_type::kInstallPermitResponse,
        request, ctx);
}

// ---------------------------------------------------------------------------
// 6. 执行事件（需 EventResponse 水位 -> exchange）
// ---------------------------------------------------------------------------
EventResponse OtaCloudProxyViaTransport::reportEvent(const ExecutionEvent& request,
                                                     const CallContext& ctx) {
    return exchangeCall<ExecutionEvent, EventResponse>(
        payload_type::kExecutionEvent, payload_type::kEventResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 7. 控制回执
// ---------------------------------------------------------------------------
ControlAckResponse OtaCloudProxyViaTransport::acknowledgeControl(const ControlAck& request,
                                                                 const CallContext& ctx) {
    return exchangeCall<ControlAck, ControlAckResponse>(
        payload_type::kControlAck, payload_type::kControlAckResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 8. 最终结果
// ---------------------------------------------------------------------------
FinalResultResponse OtaCloudProxyViaTransport::reportFinalResult(const FinalResult& request,
                                                                 const CallContext& ctx) {
    return exchangeCall<FinalResult, FinalResultResponse>(
        payload_type::kFinalResult, payload_type::kFinalResultResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 9. 日志上传凭证
// ---------------------------------------------------------------------------
LogGrantResponse OtaCloudProxyViaTransport::requestLogUpload(const LogGrantRequest& request,
                                                             const CallContext& ctx) {
    return exchangeCall<LogGrantRequest, LogGrantResponse>(
        payload_type::kLogGrantRequest, payload_type::kLogGrantResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 10. 日志上传结果
// ---------------------------------------------------------------------------
LogResultResponse OtaCloudProxyViaTransport::reportLogUpload(const LogUploadResult& request,
                                                             const CallContext& ctx) {
    return exchangeCall<LogUploadResult, LogResultResponse>(
        payload_type::kLogUploadResult, payload_type::kLogResultResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 11. 对账
// ---------------------------------------------------------------------------
ReconcileResponse OtaCloudProxyViaTransport::reconcile(const ReconcileRequest& request,
                                                       const CallContext& ctx) {
    return exchangeCall<ReconcileRequest, ReconcileResponse>(
        payload_type::kReconcileRequest, payload_type::kReconcileResponse, request, ctx);
}

// ---------------------------------------------------------------------------
// 12. 策略同步
// ---------------------------------------------------------------------------
PolicyResponse OtaCloudProxyViaTransport::syncPolicy(const PolicyRequest& request,
                                                     const CallContext& ctx) {
    return exchangeCall<PolicyRequest, PolicyResponse>(
        payload_type::kPolicyRequest, payload_type::kPolicyResponse, request, ctx);
}

} // namespace ota
} // namespace cgw_fota
