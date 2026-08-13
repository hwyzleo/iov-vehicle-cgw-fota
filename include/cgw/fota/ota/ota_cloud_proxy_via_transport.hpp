#pragma once

// =============================================================================
// include/cgw/fota/ota/ota_cloud_proxy_via_transport.hpp
// CGW-FOTA OtaCloudProxy 通用传输适配器 (CGW-FOTA-DSN-CR-010 §架构决策/接口设计)
// =============================================================================
// OtaCloudProxyViaTransport 是 OtaCloudProxy 的具体实现：在稳定的
// VehicleMessageTransport 端口之上，把 CR-009 的 12 个强类型 OTA 业务方法逐一
// 迁移到通用 exchange/publish。OtaOrchestrator 继续只依赖强类型 OtaCloudProxy
// 接口，本类不推进任何 Task/VehicleTask/Execution 状态机。
//
// 职责：
//   * 选择 payloadType、序列化/反序列化 vehicle.ota.v1（经 OtaMessageCodec）。
//   * 校验业务响应类型与 correlation；将 TransportOutcome 映射为 OtaCloudException。
//   * 下行经 subscribeDownlink 订阅，按 payloadType 解码后交业务回调。
// =============================================================================

#include "cgw/fota/ota/ota_cloud_proxy.hpp"
#include "cgw/fota/ota/ota_message_codec.hpp"
#include "cgw/fota/ota/payload_types.hpp"
#include "cgw/fota/ota/vehicle_message_transport.hpp"

#include <cstddef>
#include <string_view>
#include <utility>

namespace cgw_fota {
namespace ota {

class OtaCloudProxyViaTransport : public OtaCloudProxy {
public:
    explicit OtaCloudProxyViaTransport(VehicleMessageTransport& transport,
                                       std::size_t maxResponseBytes = 0);
    ~OtaCloudProxyViaTransport() override = default;

    // ---- 强类型业务方法（CR-009 接口不变，内部经通用 exchange）----
    ::vehicle::ota::v1::TaskCheckResponse
    checkTask(const ::vehicle::ota::v1::TaskCheckRequest& request,
              const CallContext& ctx) override;
    ::vehicle::ota::v1::ConsentResponse
    reportConsent(const ::vehicle::ota::v1::ConsentReport& request,
                  const CallContext& ctx) override;
    ::vehicle::ota::v1::DownloadGrantResponse
    requestDownload(const ::vehicle::ota::v1::DownloadGrantRequest& request,
                    const CallContext& ctx) override;
    ::vehicle::ota::v1::StageResultResponse
    reportStageResult(const ::vehicle::ota::v1::StageResultReport& request,
                      const CallContext& ctx) override;
    ::vehicle::ota::v1::InstallPermitResponse
    requestInstall(const ::vehicle::ota::v1::InstallPermitRequest& request,
                   const CallContext& ctx) override;
    ::vehicle::ota::v1::EventResponse
    reportEvent(const ::vehicle::ota::v1::ExecutionEvent& request,
                const CallContext& ctx) override;
    ::vehicle::ota::v1::ControlAckResponse
    acknowledgeControl(const ::vehicle::ota::v1::ControlAck& request,
                       const CallContext& ctx) override;
    ::vehicle::ota::v1::FinalResultResponse
    reportFinalResult(const ::vehicle::ota::v1::FinalResult& request,
                      const CallContext& ctx) override;
    ::vehicle::ota::v1::LogGrantResponse
    requestLogUpload(const ::vehicle::ota::v1::LogGrantRequest& request,
                     const CallContext& ctx) override;
    ::vehicle::ota::v1::LogResultResponse
    reportLogUpload(const ::vehicle::ota::v1::LogUploadResult& request,
                    const CallContext& ctx) override;
    ::vehicle::ota::v1::ReconcileResponse
    reconcile(const ::vehicle::ota::v1::ReconcileRequest& request,
              const CallContext& ctx) override;
    ::vehicle::ota::v1::PolicyResponse
    syncPolicy(const ::vehicle::ota::v1::PolicyRequest& request,
               const CallContext& ctx) override;

    // ---- 下行 ----
    // 订阅 OTA 业务域下行入口（透明转发给 transport；解码由回调经 decodeDownlink 完成）。
    Subscription subscribeDownlink(std::string_view service, DownlinkHandler handler);

    // 按 payloadType 解码下行消息（事件形态）为强类型业务消息。
    template <typename Message>
    Expected<Message> decodeDownlink(std::string_view ptype,
                                     const VehicleMessage& msg) const {
        return codec_.decodeDownlink<Message>(ptype, msg);
    }

private:
    VehicleMessageTransport& transport_;
    std::size_t maxResponseBytes_;
    OtaMessageCodec codec_;

    ExchangeOptions makeOptions(const CallContext& ctx) const;
    void throwFor(TransportOutcome outcome, std::string_view action) const;

    // 通用 exchange 封装：encode -> exchange -> outcome 映射 -> decode -> 返回强类型响应。
    template <typename Request, typename Response>
    Response exchangeCall(std::string_view reqType, std::string_view respType,
                          const Request& request, const CallContext& ctx) {
        auto ids = codec_.identityFrom(request, ctx);
        auto msg = codec_.encodeRequest(reqType, request, ids, ctx);
        auto result = transport_.exchange(msg, makeOptions(ctx), ctx);
        if (result.outcome != TransportOutcome::Accepted) {
            throwFor(result.outcome, reqType);
        }
        auto decoded = codec_.decodeResponse<Response>(respType, result.value, ids.messageId);
        if (!decoded) {
            throw OtaCloudException(OtaCloudException::Kind::Codec,
                                    decoded.error.frameworkCauseCode,
                                    std::string("codec: ") + decoded.error.detail);
        }
        return std::move(*decoded);
    }
};

} // namespace ota
} // namespace cgw_fota
