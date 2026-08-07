// =============================================================================
// src/someip/fota_provider.cpp
// CGW-FOTA Provider 适配器实现 (CGW-FOTA-DSN-CR-007 §Provider 设计)
// =============================================================================

#include "cgw/fota/someip/fota_provider.hpp"
#include "constants.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"

#include <cstring>
#include <string>
#include <vector>

namespace cgw_fota {
namespace someip {

using cgw::fw::someip::MethodResult;
using cgw::fw::someip::Payload;
using cgw::fw::someip::PayloadView;
using cgw::fw::someip::RequestContext;

namespace {

constexpr std::uint8_t SCHEMA_VERSION = 1;
// 受理 payload 上限（防止超限导致 codec 失败 -> CGW-FW-0306）
constexpr std::size_t MAX_REQUEST_PAYLOAD = 8 * 1024;
constexpr std::size_t MAX_FIELD_LEN = 1024;

// ---- IDL codec (big-endian, 显式字段) ----

bool readU8(const std::uint8_t* data, std::size_t size, std::size_t& off,
            std::uint8_t& out) {
    if (off + 1 > size) return false;
    out = data[off++];
    return true;
}

bool readU16BE(const std::uint8_t* data, std::size_t size, std::size_t& off,
               std::uint16_t& out) {
    if (off + 2 > size) return false;
    out = (static_cast<std::uint16_t>(data[off]) << 8) | data[off + 1];
    off += 2;
    return true;
}

bool readLenString(const std::uint8_t* data, std::size_t size, std::size_t& off,
                   std::string& out) {
    std::uint16_t len = 0;
    if (!readU16BE(data, size, off, len)) return false;
    if (len > MAX_FIELD_LEN) return false;
    if (off + len > size) return false;
    out.assign(reinterpret_cast<const char*>(data + off), len);
    off += len;
    return true;
}

void putU8(Payload& p, std::uint8_t v) { p.push_back(v); }
void putU16BE(Payload& p, std::uint16_t v) {
    p.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    p.push_back(static_cast<std::uint8_t>(v & 0xFF));
}
void putU64BE(Payload& p, std::uint64_t v) {
    for (int i = 7; i >= 0; --i) {
        p.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFF));
    }
}
void putLenString(Payload& p, const std::string& s) {
    putU16BE(p, static_cast<std::uint16_t>(s.size()));
    p.insert(p.end(), s.begin(), s.end());
}

struct DecodedRequest {
    std::uint8_t schemaVersion{0};
    std::string requestId;
    std::string reason;
    bool hasMetadata{false};
    std::string traceId;
    std::string caller;
    std::uint8_t metadataSchemaVersion{0};
};

// 解码入站请求。失败返回 false（调用方返回 CGW-FW-0306 wire error）。
bool decodeRequest(PayloadView pv, DecodedRequest& out) {
    if (pv.size > MAX_REQUEST_PAYLOAD) return false;
    if (pv.size == 0 || pv.data == nullptr) return false;
    const std::uint8_t* data = pv.data;
    std::size_t off = 0;

    if (!readU8(data, pv.size, off, out.schemaVersion)) return false;
    if (out.schemaVersion != SCHEMA_VERSION) return false;  // major 不兼容
    if (!readLenString(data, pv.size, off, out.requestId)) return false;
    if (out.requestId.empty()) return false;
    if (!readLenString(data, pv.size, off, out.reason)) return false;

    std::uint8_t hasMeta = 0;
    if (!readU8(data, pv.size, off, hasMeta)) return false;
    out.hasMetadata = (hasMeta != 0);
    if (out.hasMetadata) {
        if (!readLenString(data, pv.size, off, out.traceId)) return false;
        if (!readLenString(data, pv.size, off, out.caller)) return false;
        if (!readU8(data, pv.size, off, out.metadataSchemaVersion)) return false;
    }
    return true;
}

Payload encodeResponse(bool accepted, std::uint64_t reportId,
                       bool hasError, const std::string& errorCode,
                       const std::string& errorMsg) {
    Payload p;
    putU8(p, SCHEMA_VERSION);
    putU8(p, accepted ? 1 : 0);
    putU64BE(p, reportId);
    putU8(p, hasError ? 1 : 0);
    if (hasError) {
        putLenString(p, errorCode);
        putLenString(p, errorMsg);
    }
    return p;
}

} // namespace

FotaProviderAdapter::FotaProviderAdapter(cgw::fw::someip::Provider provider,
                                         std::shared_ptr<InventoryReporter> reporter,
                                         std::chrono::milliseconds acceptBudget)
    : provider_(std::move(provider))
    , reporter_(std::move(reporter))
    , acceptBudget_(acceptBudget)
    , hasProvider_(true) {}

FotaProviderAdapter::FotaProviderAdapter(std::shared_ptr<InventoryReporter> reporter,
                                         std::chrono::milliseconds acceptBudget)
    : reporter_(std::move(reporter))
    , acceptBudget_(acceptBudget)
    , hasProvider_(false) {}

FotaProviderAdapter::~FotaProviderAdapter() = default;

void FotaProviderAdapter::registerInventoryHandler() {
    if (!hasProvider_) return;

    FotaLogAdapter::orchestrator().info(
        "fota.provider.handler_registered",
        "Registered inventory method handler",
        {flog::f_str("someip_service_id", hex_id(FOTA_PROVIDER_SERVICE_ID)),
         flog::f_str("someip_method_id", hex_id(METHOD_REQUEST_SOFTWARE_INVENTORY)),
         flog::f_int("accept_budget_ms", static_cast<int64_t>(acceptBudget_.count()))});

    auto reporter = reporter_;  // 捕获不可变引用（CR-007 §Trace 上下文）
    provider_.registerMethod(METHOD_REQUEST_SOFTWARE_INVENTORY,
        [reporter](const RequestContext& ctx, PayloadView pv) -> MethodResult {
            // 1. 校验 payload/codec/major version
            DecodedRequest req;
            if (!decodeRequest(pv, req)) {
                return MethodResult::error(/*CGW-FW-0306*/ 0x0306,
                    "invalid request payload/codec/version");
            }

            // 2. 建立 TraceContext（traceId from optional metadata or generated）
            std::string traceId = req.hasMetadata && !req.traceId.empty()
                ? req.traceId : generate_trace_id();
            auto scope = make_someip_context_scope(
                traceId, req.requestId,
                hex_id(FOTA_PROVIDER_SERVICE_ID),
                hex_id(METHOD_REQUEST_SOFTWARE_INVENTORY),
                hex_id(ctx.clientId));

            FotaLogAdapter::orchestrator().info(
                "fota.provider.request_received",
                "Received software inventory request",
                {flog::f_str("request_id", req.requestId),
                 flog::f_str("reason", req.reason),
                 flog::f_str("someip_service_id", hex_id(FOTA_PROVIDER_SERVICE_ID)),
                 flog::f_str("someip_method_id", hex_id(METHOD_REQUEST_SOFTWARE_INVENTORY)),
                 flog::f_str("someip_session", std::to_string(ctx.session))});

            // 3. 受理/合并（orchestrator 负责；重复 requestId 返回相同 reportId）
            AsyncReportResult result = reporter->reportInventoryAsync(
                req.requestId, req.reason);

            // 4. 编码响应（只代表受理结果，不代表最终 TBOX/MQTT 成功）
            Payload resp = encodeResponse(
                result.accepted, result.report_id, false, "", "");
            return MethodResult::ok(std::move(resp));
        });
}

void FotaProviderAdapter::offer() { if (hasProvider_) provider_.offer(); }
void FotaProviderAdapter::stopOffer() { if (hasProvider_) provider_.stopOffer(); }
bool FotaProviderAdapter::offering() const {
    return hasProvider_ ? provider_.offering() : false;
}

AsyncReportResult FotaProviderAdapter::handleRequest(const std::string& request_id,
                                                    const std::string& reason) {
    return reporter_->reportInventoryAsync(request_id, reason);
}

} // namespace someip
} // namespace cgw_fota
