#pragma once

// =============================================================================
// include/cgw/fota/ota/ota_message_codec.hpp
// CGW-FOTA OTA 消息编解码 (CGW-FOTA-DSN-CR-010 §OtaMessageCodec)
// =============================================================================
// OtaMessageCodec 负责在强类型 vehicle.ota.v1 业务消息与通用传输消息
// （VehicleMessageEnvelope + 不透明 bytes）之间编解码：
//   * 使用统一生成代码序列化/解析（proto 为 wire SSOT；禁止 JSON/YAML 作为量产 codec）。
//   * 校验 payloadType、protocolVersion、message kind、correlationId、TTL 与解析状态。
//   * 保留未知兼容字段；未知枚举不得解释为业务成功。
//   * 将 transport cause 与 OTA 业务错误分层返回（业务错误在各 Response.error_code）。
//
// 本层不得记录原始 payload、VIN、凭据、下载 URL/token 等敏感内容到日志或异常信息。
// =============================================================================

#include "cgw/fota/ota/call_context.hpp"
#include "cgw/fota/ota/payload_types.hpp"
#include "cgw/fota/ota/vehicle_message_transport.hpp"

#include "vehicle/common/v1/envelope.pb.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace cgw_fota {
namespace ota {

// ---------------------------------------------------------------------------
// OperationIdentity - 跨段传播的稳定业务操作身份。
// ---------------------------------------------------------------------------
struct OperationIdentity {
    std::string messageId;         // 本次传输消息 ID（请求全局唯一）
    std::string correlationId;     // 响应需回填的关联 ID（= 请求 messageId）
    std::string idempotencyKey;    // 写操作幂等键
    std::string vehicleTaskId;
    std::string executionId;
    std::string traceId;
    std::string protocolVersion;
};

// ---------------------------------------------------------------------------
// OtaProtocolError - 协议/编解码层错误（传输 cause 与业务错误分层）。
// ---------------------------------------------------------------------------
enum class OtaProtocolErrorKind {
    PayloadTypeMismatch,   // payloadType 与期望不匹配
    VersionMismatch,       // protocolVersion 不兼容
    MessageKindMismatch,   // 消息方向/形态不符
    CorrelationMismatch,   // correlationId 缺失/不匹配
    TtlExpired,            // 消息已过期
    MalformedPayload,      // payload 解析失败
    MissingField,          // 必要字段缺失
    Internal,              // 其他内部错误
};

struct OtaProtocolError {
    OtaProtocolErrorKind kind = OtaProtocolErrorKind::Internal;
    std::string detail;                 // 不含 payload/敏感内容
    std::string frameworkCauseCode;     // 保留 CGW-FW-03xx 链
};

// 带错误的结果。ok 为 true 时 value 有效。
template <typename T>
struct Expected {
    bool ok = false;
    T value{};
    OtaProtocolError error;

    explicit operator bool() const { return ok; }
    T& operator*() { return value; }
    const T& operator*() const { return value; }
    T* operator->() { return &value; }
    const T* operator->() const { return &value; }
};

// payload bytes <-> string 互转（不复制语义，仅类型转换）。
inline std::string bytesToStdString(const std::vector<std::byte>& bytes) {
    if (bytes.empty()) return {};
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

inline std::vector<std::byte> stdStringToBytes(const std::string& s) {
    std::vector<std::byte> out(s.size());
    std::transform(s.begin(), s.end(), out.begin(),
                   [](char c) { return std::byte(static_cast<unsigned char>(c)); });
    return out;
}

// ---------------------------------------------------------------------------
// OtaMessageCodec - 业务类型 <-> 通用 Envelope+bytes。
// ---------------------------------------------------------------------------
class OtaMessageCodec {
public:
    explicit OtaMessageCodec(std::string expectedProtocolVersion = std::string(payload_type::kProtocolVersion))
        : expectedProtocolVersion_(std::move(expectedProtocolVersion)) {}

    // 从业务请求的 embedded RequestEnvelope + 调用上下文构造稳定操作身份。
    template <typename Request>
    OperationIdentity identityFrom(const Request& request, const CallContext& ctx) const {
        OperationIdentity ids;
        const auto& env = request.envelope();
        ids.messageId = !env.request_id().empty() ? env.request_id() : ctx.requestId;
        ids.correlationId = ids.messageId;
        ids.idempotencyKey = !env.idempotency_key().empty() ? env.idempotency_key()
                                                            : ctx.idempotencyKey;
        ids.vehicleTaskId = env.vehicle_task_id();
        ids.executionId = env.execution_id();
        ids.traceId = !env.trace_id().empty() ? env.trace_id() : ctx.traceId;
        ids.protocolVersion = !env.protocol_version().empty() ? env.protocol_version()
                                                              : expectedProtocolVersion_;
        return ids;
    }

    // 编码业务请求 -> 通用传输消息（Envelope + payload bytes）。
    template <typename Request>
    VehicleMessage encodeRequest(std::string_view payload_type, const Request& request,
                                 const OperationIdentity& ids, const CallContext& ctx) const {
        VehicleMessage msg;
        auto& env = msg.envelope;
        env.set_message_id(ids.messageId);
        if (!ids.correlationId.empty()) env.set_correlation_id(ids.correlationId);
        env.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_REQUEST);
        env.set_service(std::string(payload_type::kService));
        env.set_payload_type(std::string(payload_type));
        env.set_protocol_version(ids.protocolVersion);
        env.set_timestamp_ms(nowMs());
        env.set_expire_at_ms(ctx.resolveDeadline(nowMs()));
        if (!ids.idempotencyKey.empty()) env.set_idempotency_key(ids.idempotencyKey);
        if (!ids.vehicleTaskId.empty()) env.set_vehicle_task_id(ids.vehicleTaskId);
        if (!ids.executionId.empty()) env.set_execution_id(ids.executionId);
        if (!ids.traceId.empty()) env.set_trace_id(ids.traceId);

        std::string payload;
        if (!request.SerializeToString(&payload)) {
            // 编码失败：空 payload，由适配层按协议错误处理。
            msg.envelope.set_payload_type(std::string(payload_type));
        }
        msg.payload = stdStringToBytes(payload);
        return msg;
    }

    // 解码业务响应。校验 message kind / correlation / payloadType / version / TTL 后解析。
    template <typename Response>
    Expected<Response> decodeResponse(std::string_view expected_payload_type,
                                      const VehicleMessage& response,
                                      std::string_view expectedCorrelationId) const {
        return decode<Response>(::vehicle::common::v1::MESSAGE_KIND_RESPONSE,
                                expected_payload_type, response, expectedCorrelationId);
    }

    // 解码下行消息（事件形态）。用于 subscribe 下行回调按 payloadType 解码。
    template <typename Message>
    Expected<Message> decodeDownlink(std::string_view expected_payload_type,
                                     const VehicleMessage& msg,
                                     std::string_view expectedCorrelationId = {}) const {
        return decode<Message>(::vehicle::common::v1::MESSAGE_KIND_EVENT,
                               expected_payload_type, msg, expectedCorrelationId);
    }

private:
    std::string expectedProtocolVersion_;

    static std::int64_t nowMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch()).count();
    }

    static OtaProtocolError makeError(OtaProtocolErrorKind kind, std::string detail) {
        OtaProtocolError e;
        e.kind = kind;
        e.detail = std::move(detail);
        e.frameworkCauseCode = "CGW-FW-0306";
        return e;
    }

    bool compatibleVersion(std::string_view candidate) const {
        // 当前 MVP 为精确匹配；Envelope major 不兼容变化必须建立链路配对 CR。
        return candidate == expectedProtocolVersion_;
    }

    template <typename Message>
    Expected<Message> decode(::vehicle::common::v1::MessageKind expectedKind,
                             std::string_view expected_payload_type,
                             const VehicleMessage& msg,
                             std::string_view expectedCorrelationId) const {
        Expected<Message> out;
        const auto& env = msg.envelope;

        if (env.message_kind() != expectedKind) {
            out.error = makeError(OtaProtocolErrorKind::MessageKindMismatch,
                                  "message kind mismatch");
            return out;
        }
        if (env.payload_type() != expected_payload_type) {
            out.error = makeError(OtaProtocolErrorKind::PayloadTypeMismatch,
                                  "payload_type mismatch");
            return out;
        }
        if (!compatibleVersion(env.protocol_version())) {
            out.error = makeError(OtaProtocolErrorKind::VersionMismatch,
                                  "protocol_version mismatch");
            return out;
        }
        if (!expectedCorrelationId.empty() && env.correlation_id() != expectedCorrelationId) {
            out.error = makeError(OtaProtocolErrorKind::CorrelationMismatch,
                                  "correlation_id mismatch");
            return out;
        }
        if (env.expire_at_ms() > 0 && env.expire_at_ms() < nowMs()) {
            out.error = makeError(OtaProtocolErrorKind::TtlExpired, "message expired");
            return out;
        }
        if (msg.payload.empty()) {
            out.error = makeError(OtaProtocolErrorKind::MissingField, "empty payload");
            return out;
        }
        std::string raw = bytesToStdString(msg.payload);
        if (!out.value.ParseFromString(raw)) {
            out.error = makeError(OtaProtocolErrorKind::MalformedPayload,
                                  "payload parse failed");
            return out;
        }
        out.ok = true;
        return out;
    }
};

} // namespace ota
} // namespace cgw_fota
