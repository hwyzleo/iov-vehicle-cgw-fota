#pragma once

// =============================================================================
// include/cgw/fota/ota/ota_cloud_proxy.hpp
// CGW-FOTA 车云 OTA 代理接口 (CGW-FOTA-DSN-CR-009 §组件设计/核心接口)
// =============================================================================
// CGW-FOTA 不直接建立 MQTT/HTTP 云连接；所有车云业务调用经 TBOX 的 OtaCloudProxy，
// 沿用 CGW-FOTA ↔ TBOX SOME/IP 和 TBOX ↔ 云端安全通道。业务 payload 统一 Protobuf。
//
// 方法名是模块内部语义名；SOME/IP Method/Event ID 必须由 Service Registry 和权威
// IDL 分配，不在本 CR 中硬编码。
//
// 传输/框架失败（timeout/unavailable/codec）抛 OtaCloudException；业务错误通过
// 各 Response 的 error_code/error_detail 字段返回。framework 自动重试关闭，业务
// 重试由 OtaOrchestrator 使用 durable 状态和同一幂等身份执行。
// =============================================================================

#include "cgw/fota/ota/call_context.hpp"

#include "vehicle/common/v1/envelope.pb.h"
#include "vehicle/ota/v1/task.pb.h"
#include "vehicle/ota/v1/consent.pb.h"
#include "vehicle/ota/v1/package.pb.h"
#include "vehicle/ota/v1/execution.pb.h"
#include "vehicle/ota/v1/control.pb.h"
#include "vehicle/ota/v1/log.pb.h"
#include "vehicle/ota/v1/policy.pb.h"
#include "vehicle/ota/v1/reconcile.pb.h"

#include <stdexcept>
#include <string>

namespace cgw_fota {
namespace ota {

// ---------------------------------------------------------------------------
// OtaCloudException - 传输/框架失败（业务错误见各 Response.error_code）
// frameworkCauseCode 保留 CGW-FW-03xx 链，不得丢失。
// ---------------------------------------------------------------------------
class OtaCloudException : public std::runtime_error {
public:
    enum class Kind {
        Timeout,        // 调用超时 -> 业务映射 OTA-*-TIMEOUT
        Unavailable,    // TBOX/服务不可用 -> OTA-*-UNAVAILABLE
        Codec,          // payload/codec 失败 -> OTA-*-CODEC
        Internal,       // runtime/resource/shutdown
    };

    OtaCloudException(Kind k, std::string frameworkCauseCode, std::string detail)
        : std::runtime_error(detail)
        , kind_(k)
        , frameworkCauseCode_(std::move(frameworkCauseCode)) {}

    Kind kind() const { return kind_; }
    const std::string& frameworkCauseCode() const { return frameworkCauseCode_; }

private:
    Kind kind_;
    std::string frameworkCauseCode_;
};

// ---------------------------------------------------------------------------
// OtaCloudProxy - 车云 OTA 业务代理（纯虚接口，供真实 SOME/IP 适配与 Mock/桩实现）
// ---------------------------------------------------------------------------
class OtaCloudProxy {
public:
    virtual ~OtaCloudProxy() = default;

    // 1. 任务检测
    virtual ::vehicle::ota::v1::TaskCheckResponse
    checkTask(const ::vehicle::ota::v1::TaskCheckRequest& req, const CallContext& ctx) = 0;

    // 2. 授权结果上报
    virtual ::vehicle::ota::v1::ConsentResponse
    reportConsent(const ::vehicle::ota::v1::ConsentReport& req, const CallContext& ctx) = 0;

    // 3. 下载授权
    virtual ::vehicle::ota::v1::DownloadGrantResponse
    requestDownload(const ::vehicle::ota::v1::DownloadGrantRequest& req, const CallContext& ctx) = 0;

    // 4. 下载/校验阶段结果
    virtual ::vehicle::ota::v1::StageResultResponse
    reportStageResult(const ::vehicle::ota::v1::StageResultReport& req, const CallContext& ctx) = 0;

    // 5. 安装许可
    virtual ::vehicle::ota::v1::InstallPermitResponse
    requestInstall(const ::vehicle::ota::v1::InstallPermitRequest& req, const CallContext& ctx) = 0;

    // 6. 执行事件
    virtual ::vehicle::ota::v1::EventResponse
    reportEvent(const ::vehicle::ota::v1::ExecutionEvent& req, const CallContext& ctx) = 0;

    // 7. 控制回执
    virtual ::vehicle::ota::v1::ControlAckResponse
    acknowledgeControl(const ::vehicle::ota::v1::ControlAck& req, const CallContext& ctx) = 0;

    // 8. 最终结果
    virtual ::vehicle::ota::v1::FinalResultResponse
    reportFinalResult(const ::vehicle::ota::v1::FinalResult& req, const CallContext& ctx) = 0;

    // 9. 日志上传凭证
    virtual ::vehicle::ota::v1::LogGrantResponse
    requestLogUpload(const ::vehicle::ota::v1::LogGrantRequest& req, const CallContext& ctx) = 0;

    // 10. 日志上传结果
    virtual ::vehicle::ota::v1::LogResultResponse
    reportLogUpload(const ::vehicle::ota::v1::LogUploadResult& req, const CallContext& ctx) = 0;

    // 11. 对账
    virtual ::vehicle::ota::v1::ReconcileResponse
    reconcile(const ::vehicle::ota::v1::ReconcileRequest& req, const CallContext& ctx) = 0;

    // 12. 策略同步
    virtual ::vehicle::ota::v1::PolicyResponse
    syncPolicy(const ::vehicle::ota::v1::PolicyRequest& req, const CallContext& ctx) = 0;
};

} // namespace ota
} // namespace cgw_fota
