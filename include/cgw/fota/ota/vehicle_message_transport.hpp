#pragma once

// =============================================================================
// include/cgw/fota/ota/vehicle_message_transport.hpp
// CGW-FOTA 通用车云消息传输端口 (CGW-FOTA-DSN-CR-010 §接口设计/通用传输端口)
// =============================================================================
// VehicleMessageTransport 是稳定传输端口：OtaCloudProxy 之下只接受公共 Envelope、
// 二进制 payload、调用上下文和超时，不依赖任何 OTA 生成类型。量产
// SomeIpVehicleMessageTransport 与测试 FakeVehicleMessageTransport 实现同一端口。
//
// 语义约束：
//   * exchange 只表示通用请求/响应交互，不代表业务成功。
//   * publish 用于无需同步业务响应的事件；需要 EventResponse 水位的 OTA 事件仍用 exchange。
//   * subscribe 注册 OTA 业务域下行入口；callback 不得在 SOME/IP I/O 线程推进状态机。
//   * TransportOutcome 只描述本地受理/可用性/版本/超时/容量/协议结果；业务成功由
//     各 Response 业务字段表达。
//   * 本层不得记录原始 payload、VIN、凭据、下载 URL/token 等敏感内容到日志。
// =============================================================================

#include "cgw/fota/ota/call_context.hpp"

#include "vehicle/common/v1/envelope.pb.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace cgw_fota {
namespace ota {

// ---------------------------------------------------------------------------
// TransportOutcome - 传输层结果。只描述本地受理/可用性/版本/超时/容量/协议结果。
// ---------------------------------------------------------------------------
enum class TransportOutcome {
    Accepted,          // 本地受理；不代表业务成功
    Rejected,          // 受理被拒绝（TTL 过期/方向/服务/关联等）
    Timeout,           // 调用超时
    Unknown,           // 未知/不确定结果
    Unavailable,       // TBOX/服务不可用
    VersionMismatch,   // Envelope/协议版本不兼容
    PayloadTooLarge,   // 超出大小上限
    ProtocolError,     // 协议/编解码错误
    Stopping,          // 正在关闭，不再受理新调用
};

// 传输结果。T 为空（publish）时有专门特化。
template <typename T>
struct TransportResult {
    TransportOutcome outcome = TransportOutcome::Unknown;
    T value{};
};

// publish（无需同步响应）特化：只有 outcome。
template <>
struct TransportResult<void> {
    TransportOutcome outcome = TransportOutcome::Unknown;
};

// ---------------------------------------------------------------------------
// VehicleMessage - 公共 Envelope + 不透明 payload bytes。
// Envelope 只承载传输元数据与路由；payload 为 vehicle.ota.v1 序列化 bytes。
// ---------------------------------------------------------------------------
struct VehicleMessage {
    ::vehicle::common::v1::VehicleMessageEnvelope envelope;
    std::vector<std::byte> payload;
};

// 请求/响应交互选项。
struct ExchangeOptions {
    std::chrono::milliseconds timeout{10000};
    std::size_t max_response_bytes = 0;   // 0 = 不限制
};

// 下行回调。接收校验通过的下行消息；实现不得在传输 I/O 线程推进状态机。
using DownlinkHandler = std::function<void(VehicleMessage&&)>;

// ---------------------------------------------------------------------------
// Subscription - 订阅句柄（RAII；析构/取消即退订）。
// ---------------------------------------------------------------------------
class Subscription {
public:
    Subscription() = default;
    explicit Subscription(std::function<void()> unsubscribe)
        : unsubscribe_(std::move(unsubscribe)) {}

    Subscription(Subscription&&) = default;
    Subscription& operator=(Subscription&&) = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    ~Subscription() { cancel(); }

    // 显式退订（幂等）。
    void cancel() {
        if (unsubscribe_) {
            auto fn = std::move(unsubscribe_);
            unsubscribe_ = nullptr;
            fn();
        }
    }

private:
    std::function<void()> unsubscribe_;
};

// ---------------------------------------------------------------------------
// VehicleMessageTransport - 通用车云消息传输端口（纯虚接口）。
// 量产 SomeIpVehicleMessageTransport 与测试 FakeVehicleMessageTransport 实现同一端口。
// ---------------------------------------------------------------------------
class VehicleMessageTransport {
public:
    virtual ~VehicleMessageTransport() = default;

    // 通用请求/响应交互。只表示传输层受理，不代表业务成功。
    virtual TransportResult<VehicleMessage> exchange(
        const VehicleMessage& msg, const ExchangeOptions& opts, const CallContext& ctx) = 0;

    // 单向事件投递（无需同步业务响应）。
    virtual TransportResult<void> publish(const VehicleMessage& msg, const CallContext& ctx) = 0;

    // 注册 OTA 业务域下行入口（按 service 过滤）。
    virtual Subscription subscribe(std::string_view service, DownlinkHandler handler) = 0;
};

} // namespace ota
} // namespace cgw_fota
