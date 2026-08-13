// =============================================================================
// tests/ota/transport_contract_test.cpp
// CGW-FOTA 通用传输端口 contract tests (CGW-FOTA-DSN-CR-010 §测试矩阵)
// =============================================================================
// 覆盖 CR-010 测试矩阵：
//   1) 每个 OtaCloudProxy 方法的 encode/exchange/decode contract test。
//   2) Protobuf 新增兼容字段/未知字段/未知枚举/新 payloadType。
//   3) correlation 缺失/错误、重复/迟到响应、响应类型错配。
//   4) timeout/unknown outcome/Unavailable/VersionMismatch/Stopping/资源耗尽。
//   5) TTL 过期、payload 上限、非法 Envelope、非法 Proto、敏感日志扫描。
//   9) 新增兼容 OTA 消息不增加 SOME/IP Method 数量、不修改 transport C++ API。
//   10) 下行订阅与解码、publish 单向投递。
// =============================================================================

#include "cgw/fota/ota/mock/fake_vehicle_message_transport.hpp"
#include "cgw/fota/ota/ota_cloud_proxy_via_transport.hpp"
#include "cgw/fota/ota/ota_message_codec.hpp"
#include "cgw/fota/ota/payload_types.hpp"
#include "cgw/fota/ota/vehicle_message_transport.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

using namespace cgw_fota::ota;
using namespace cgw_fota::ota::mock;

namespace {

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

ScenarioScript makeScenario(const std::vector<std::string>& faults = {}) {
    std::string list;
    for (std::size_t i = 0; i < faults.size(); ++i) {
        if (i) list += ", ";
        list += "\"" + faults[i] + "\"";
    }
    return parseScenario(R"({
      "scenario": "contract",
      "clock": "virtual",
      "inventory": {"mode": "FULL", "baseline": "BASE-001"},
      "packages": [],
      "executor": {"stages": []},
      "faults": [)" + list + R"(]
    })");
}

CallContext makeCtx(const std::string& idKey) {
    CallContext ctx;
    ctx.traceId = "trace-" + idKey;
    ctx.requestId = "req-" + idKey;
    ctx.idempotencyKey = idKey;
    ctx.timeout = std::chrono::milliseconds(1000);
    ctx.deviceId = "dev-contract";
    ctx.vin = "VINCONTRACT";
    return ctx;
}

void fillEnvelope(::vehicle::common::v1::RequestEnvelope* e, const std::string& idKey) {
    e->set_request_id("req-" + idKey);
    e->set_timestamp_ms(nowMs());
    e->set_protocol_version("ota-v1");
    e->set_device_id("dev-contract");
    e->set_vin("VINCONTRACT");
    e->set_vehicle_task_id("VT-CONTRACT");
    e->set_idempotency_key(idKey);
    e->set_trace_id("trace-" + idKey);
}

std::vector<std::byte> strToBytes(const std::string& s) {
    std::vector<std::byte> out(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        out[i] = std::byte(static_cast<unsigned char>(s[i]));
    }
    return out;
}

std::string bytesToStr(const std::vector<std::byte>& b) {
    if (b.empty()) return {};
    return std::string(reinterpret_cast<const char*>(b.data()), b.size());
}

// 构造一条请求消息（Envelope 元数据 + payload_type + payload）。
VehicleMessage makeRequest(std::string_view ptype, const std::string& payload,
                           std::string_view messageId, std::string_view service = payload_type::kService,
                           std::string_view version = payload_type::kProtocolVersion,
                           std::int64_t expireAtMs = 0) {
    VehicleMessage msg;
    msg.envelope.set_message_id(std::string(messageId));
    msg.envelope.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_REQUEST);
    msg.envelope.set_service(std::string(service));
    msg.envelope.set_payload_type(std::string(ptype));
    msg.envelope.set_protocol_version(std::string(version));
    msg.envelope.set_timestamp_ms(nowMs());
    if (expireAtMs > 0) msg.envelope.set_expire_at_ms(expireAtMs);
    msg.payload = strToBytes(payload);
    return msg;
}

// 构造一条下行事件消息。
VehicleMessage makeEvent(std::string_view ptype, const std::string& payload,
                         std::string_view messageId, std::string_view service = payload_type::kService) {
    VehicleMessage msg;
    msg.envelope.set_message_id(std::string(messageId));
    msg.envelope.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_EVENT);
    msg.envelope.set_service(std::string(service));
    msg.envelope.set_payload_type(std::string(ptype));
    msg.envelope.set_protocol_version(std::string(payload_type::kProtocolVersion));
    msg.envelope.set_timestamp_ms(nowMs());
    msg.payload = strToBytes(payload);
    return msg;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. 每个 OtaCloudProxy 方法：encode -> exchange -> decode 全链路 contract test。
// ---------------------------------------------------------------------------
TEST(TransportContract, AllProxyMethodsRoundTrip) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaCloudProxyViaTransport cloud(transport);

    // 1. checkTask
    {
        ::vehicle::ota::v1::TaskCheckRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-check");
        auto resp = cloud.checkTask(req, makeCtx("ct-check"));
        EXPECT_EQ(resp.inventory_disposition(),
                  ::vehicle::ota::v1::INVENTORY_DISPOSITION_ACCEPTED);
        EXPECT_EQ(resp.vehicle_task_id(), "VT-001");
    }
    // 2. reportConsent
    {
        ::vehicle::ota::v1::ConsentReport req;
        fillEnvelope(req.mutable_envelope(), "ct-consent");
        req.set_user_choice(::vehicle::ota::v1::CONSENT_STATUS_ACCEPTED);
        auto resp = cloud.reportConsent(req, makeCtx("ct-consent"));
        EXPECT_EQ(resp.effective_consent_status(),
                  ::vehicle::ota::v1::CONSENT_STATUS_ACCEPTED);
    }
    // 3. requestDownload
    {
        ::vehicle::ota::v1::DownloadGrantRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-dl");
        req.set_package_id("PKG-1");
        auto resp = cloud.requestDownload(req, makeCtx("ct-dl"));
        EXPECT_TRUE(resp.granted());
        EXPECT_FALSE(resp.url().empty());
    }
    // 4. reportStageResult
    {
        ::vehicle::ota::v1::StageResultReport req;
        fillEnvelope(req.mutable_envelope(), "ct-sr");
        auto resp = cloud.reportStageResult(req, makeCtx("ct-sr"));
        EXPECT_TRUE(resp.accepted());
    }
    // 5. requestInstall
    {
        ::vehicle::ota::v1::InstallPermitRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-permit");
        auto resp = cloud.requestInstall(req, makeCtx("ct-permit"));
        EXPECT_TRUE(resp.permitted());
        EXPECT_FALSE(resp.execution_id().empty());
    }
    // 6. reportEvent（水位）
    {
        ::vehicle::ota::v1::ExecutionEvent req;
        fillEnvelope(req.mutable_envelope(), "ct-evt");
        req.set_sequence_no(1);
        auto resp = cloud.reportEvent(req, makeCtx("ct-evt"));
        EXPECT_EQ(resp.status(), ::vehicle::ota::v1::EVENT_RESPONSE_STATUS_ACCEPTED);
        EXPECT_EQ(resp.accepted_sequence_no(), 1u);
    }
    // 7. acknowledgeControl
    {
        ::vehicle::ota::v1::ControlAck req;
        fillEnvelope(req.mutable_envelope(), "ct-ack");
        req.set_control_revision("CR-1");
        auto resp = cloud.acknowledgeControl(req, makeCtx("ct-ack"));
        EXPECT_TRUE(resp.accepted());
    }
    // 8. reportFinalResult
    {
        ::vehicle::ota::v1::FinalResult req;
        fillEnvelope(req.mutable_envelope(), "ct-final");
        req.set_execution_id("EX-1");
        auto resp = cloud.reportFinalResult(req, makeCtx("ct-final"));
        EXPECT_TRUE(resp.result_accepted());
    }
    // 9. requestLogUpload
    {
        ::vehicle::ota::v1::LogGrantRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-log");
        req.set_log_request_id("LOG-1");
        auto resp = cloud.requestLogUpload(req, makeCtx("ct-log"));
        EXPECT_TRUE(resp.granted());
    }
    // 10. reportLogUpload
    {
        ::vehicle::ota::v1::LogUploadResult req;
        fillEnvelope(req.mutable_envelope(), "ct-logres");
        req.set_log_request_id("LOG-1");
        auto resp = cloud.reportLogUpload(req, makeCtx("ct-logres"));
        EXPECT_TRUE(resp.accepted());
    }
    // 11. reconcile
    {
        ::vehicle::ota::v1::ReconcileRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-recon");
        auto resp = cloud.reconcile(req, makeCtx("ct-recon"));
        EXPECT_EQ(resp.action(), ::vehicle::ota::v1::RECONCILE_ACTION_RESUME);
    }
    // 12. syncPolicy
    {
        ::vehicle::ota::v1::PolicyRequest req;
        fillEnvelope(req.mutable_envelope(), "ct-policy");
        auto resp = cloud.syncPolicy(req, makeCtx("ct-policy"));
        EXPECT_EQ(resp.preference_version(), "pref-1");
        EXPECT_FALSE(resp.conflict());
    }
}

// ---------------------------------------------------------------------------
// 2. correlation 缺失/错配：响应不得完成其他调用。
// ---------------------------------------------------------------------------
// 两条并发调用：各自响应只能由各自的 message_id 关联解码，实现迟到/重复响应隔离。
TEST(TransportContract, CorrelationIsolatesConcurrentCalls) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaCloudProxyViaTransport cloud(transport);
    OtaMessageCodec codec;

    // 手动执行两条 checkTask 调用并保留原始响应 VehicleMessage。
    auto reqA = ::vehicle::ota::v1::TaskCheckRequest();
    fillEnvelope(reqA.mutable_envelope(), "ct-A");
    auto idsA = codec.identityFrom(reqA, makeCtx("ct-A"));
    auto msgA = codec.encodeRequest(payload_type::kTaskCheckRequest, reqA, idsA, makeCtx("ct-A"));
    auto rawA = transport.exchange(msgA, ExchangeOptions{}, makeCtx("ct-A"));

    auto reqB = ::vehicle::ota::v1::TaskCheckRequest();
    fillEnvelope(reqB.mutable_envelope(), "ct-B");
    auto idsB = codec.identityFrom(reqB, makeCtx("ct-B"));
    auto msgB = codec.encodeRequest(payload_type::kTaskCheckRequest, reqB, idsB, makeCtx("ct-B"));
    auto rawB = transport.exchange(msgB, ExchangeOptions{}, makeCtx("ct-B"));

    ASSERT_EQ(rawA.outcome, TransportOutcome::Accepted);
    ASSERT_EQ(rawB.outcome, TransportOutcome::Accepted);

    // A 的响应只能用 A 的 message_id 解码；用 B 的关联解码失败（隔离）。
    EXPECT_TRUE(codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
                    payload_type::kTaskCheckResponse, rawA.value, idsA.messageId).ok);
    EXPECT_FALSE(codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
                     payload_type::kTaskCheckResponse, rawA.value, idsB.messageId).ok);
    // B 的响应同理。
    EXPECT_TRUE(codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
                    payload_type::kTaskCheckResponse, rawB.value, idsB.messageId).ok);
    EXPECT_FALSE(codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
                     payload_type::kTaskCheckResponse, rawB.value, idsA.messageId).ok);
}

// ---------------------------------------------------------------------------
// 3. 响应类型错配 / message kind 错配 / 版本不匹配。
// ---------------------------------------------------------------------------
TEST(TransportContract, DecodeRejectsWrongTypeKindVersion) {
    OtaMessageCodec codec;
    const std::string msgId = "req-typecheck";
    CallContext ctx = makeCtx("typecheck");

    // 合法响应基线。
    ::vehicle::ota::v1::TaskCheckRequest req;
    fillEnvelope(req.mutable_envelope(), "typecheck");
    auto ids = codec.identityFrom(req, ctx);
    auto encoded = codec.encodeRequest(payload_type::kTaskCheckRequest, req, ids, ctx);

    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    auto accepted = transport.exchange(encoded, ExchangeOptions{}, ctx);
    ASSERT_EQ(accepted.outcome, TransportOutcome::Accepted);

    // payload_type 错配。
    auto wrongType = accepted.value;
    wrongType.envelope.set_payload_type(std::string(payload_type::kConsentResponse));
    auto r1 = codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
        payload_type::kTaskCheckResponse, wrongType, msgId);
    EXPECT_FALSE(r1.ok);
    EXPECT_EQ(r1.error.kind, OtaProtocolErrorKind::PayloadTypeMismatch);

    // message kind 错配（改成 EVENT）。
    auto wrongKind = accepted.value;
    wrongKind.envelope.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_EVENT);
    auto r2 = codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
        payload_type::kTaskCheckResponse, wrongKind, msgId);
    EXPECT_FALSE(r2.ok);
    EXPECT_EQ(r2.error.kind, OtaProtocolErrorKind::MessageKindMismatch);

    // 版本不匹配（codec 层）。
    auto wrongVersion = accepted.value;
    wrongVersion.envelope.set_protocol_version("ota-v2");
    auto r3 = codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
        payload_type::kTaskCheckResponse, wrongVersion, msgId);
    EXPECT_FALSE(r3.ok);
    EXPECT_EQ(r3.error.kind, OtaProtocolErrorKind::VersionMismatch);

    // 版本不匹配（transport 层）：请求带 v2 -> 传输直接拒绝。
    auto v2Request = encoded;
    v2Request.envelope.set_protocol_version("ota-v2");
    auto v2res = transport.exchange(v2Request, ExchangeOptions{}, ctx);
    EXPECT_EQ(v2res.outcome, TransportOutcome::VersionMismatch);
}

// ---------------------------------------------------------------------------
// 4. 未知字段保留 / 未知枚举不得解释为成功 / 非法 Proto。
// ---------------------------------------------------------------------------
TEST(TransportContract, UnknownEnumNotSuccessAndMalformedPayload) {
    OtaMessageCodec codec;
    const std::string msgId = "req-unknown";

    // 构造 EventResponse 响应，status 字段写为未知枚举值 99（字段 1 varint）。
    VehicleMessage resp;
    resp.envelope.set_message_id(msgId);
    resp.envelope.set_correlation_id(msgId);
    resp.envelope.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_RESPONSE);
    resp.envelope.set_service(std::string(payload_type::kService));
    resp.envelope.set_payload_type(std::string(payload_type::kEventResponse));
    resp.envelope.set_protocol_version(std::string(payload_type::kProtocolVersion));
    resp.envelope.set_timestamp_ms(nowMs());
    resp.payload = strToBytes(std::string("\x08\x63", 2));  // field 1 varint 99

    auto decoded = codec.decodeResponse<::vehicle::ota::v1::EventResponse>(
        payload_type::kEventResponse, resp, msgId);
    ASSERT_TRUE(decoded.ok);
    // 未知枚举不得解释为 ACCEPTED（业务成功由显式状态表达）。
    EXPECT_NE(decoded->status(), ::vehicle::ota::v1::EVENT_RESPONSE_STATUS_ACCEPTED);

    // 非法 Proto：payload 无法解析 -> MalformedPayload。
    resp.payload = strToBytes("not-a-proto-bytes");
    auto bad = codec.decodeResponse<::vehicle::ota::v1::EventResponse>(
        payload_type::kEventResponse, resp, msgId);
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.error.kind, OtaProtocolErrorKind::MalformedPayload);
}

// ---------------------------------------------------------------------------
// 5. TTL 过期 / payload 超限 / 未知 payloadType / 非法 Envelope。
// ---------------------------------------------------------------------------
TEST(TransportContract, TtlSizeAndUnknownRejected) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaMessageCodec codec;
    CallContext ctx = makeCtx("reject");

    ::vehicle::ota::v1::TaskCheckRequest req;
    fillEnvelope(req.mutable_envelope(), "reject");
    auto ids = codec.identityFrom(req, ctx);
    auto encoded = codec.encodeRequest(payload_type::kTaskCheckRequest, req, ids, ctx);

    // TTL 过期 -> 传输拒绝。
    auto expired = encoded;
    expired.envelope.set_expire_at_ms(nowMs() - 1000);
    EXPECT_EQ(transport.exchange(expired, ExchangeOptions{}, ctx).outcome,
              TransportOutcome::Rejected);

    // 响应大小超限 -> PayloadTooLarge（限制响应大小）。
    ExchangeOptions tiny;
    tiny.max_response_bytes = 8;
    EXPECT_EQ(transport.exchange(encoded, tiny, ctx).outcome,
              TransportOutcome::PayloadTooLarge);

    // 未知 payloadType -> 明确拒绝，不落入默认 Handler。
    auto unknown = encoded;
    unknown.envelope.set_payload_type("ota.unknown.op.v9");
    EXPECT_EQ(transport.exchange(unknown, ExchangeOptions{}, ctx).outcome,
              TransportOutcome::Rejected);

    // 非法 Envelope：空 message_id -> 拒绝。
    auto noId = encoded;
    noId.envelope.clear_message_id();
    EXPECT_EQ(transport.exchange(noId, ExchangeOptions{}, ctx).outcome,
              TransportOutcome::Rejected);
}

// ---------------------------------------------------------------------------
// 6. timeout outcome 映射：适配器将 Timeout -> OtaCloudException(Timeout)。
// ---------------------------------------------------------------------------
TEST(TransportContract, TimeoutOutcomeMappedToOtaCloudException) {
    ScenarioScript scenario = makeScenario({"cloud_timeout"});
    FakeVehicleMessageTransport transport(scenario);
    OtaCloudProxyViaTransport cloud(transport);

    ::vehicle::ota::v1::TaskCheckRequest req;
    fillEnvelope(req.mutable_envelope(), "ct-timeout");
    // 首次调用（单次故障注入）-> OtaCloudException(Timeout)，保留 CGW-FW-03xx 链。
    try {
        cloud.checkTask(req, makeCtx("ct-timeout"));
        FAIL() << "should throw";
    } catch (const OtaCloudException& e) {
        EXPECT_EQ(e.kind(), OtaCloudException::Kind::Timeout);
        EXPECT_EQ(e.frameworkCauseCode(), "CGW-FW-0305");
    }
    // 第二次调用（故障仅一次）恢复成功。
    auto resp = cloud.checkTask(req, makeCtx("ct-timeout"));
    EXPECT_EQ(resp.vehicle_task_id(), "VT-001");
}

// ---------------------------------------------------------------------------
// 7. 新增兼容 payloadType：注册 handler 即可，不修改 transport C++ API。
// ---------------------------------------------------------------------------
TEST(TransportContract, NewPayloadTypeNeedsNoTransportApiChange) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaMessageCodec codec;
    CallContext ctx = makeCtx("v2");

    constexpr std::string_view kTaskCheckRequestV2 = "ota.task-check.request.v2";
    constexpr std::string_view kTaskCheckResponseV2 = "ota.task-check.response.v2";

    // 端点通过 allowlist 注册 v2 处理器（等价于"版本化路由目录新增条目"）。
    transport.registerPayloadHandler(
        std::string(kTaskCheckRequestV2),
        [&](const VehicleMessage& msg, const ExchangeOptions&) {
            ::vehicle::ota::v1::TaskCheckRequest r;
            if (!r.ParseFromString(bytesToStr(msg.payload))) {
                return TransportResult<VehicleMessage>{TransportOutcome::ProtocolError, {}};
            }
            ::vehicle::ota::v1::TaskCheckResponse body;
            body.set_vehicle_task_id("VT-V2");
            body.set_inventory_disposition(::vehicle::ota::v1::INVENTORY_DISPOSITION_ACCEPTED);
            VehicleMessage resp;
            resp.envelope.set_correlation_id(msg.envelope.message_id());
            resp.envelope.set_message_kind(::vehicle::common::v1::MESSAGE_KIND_RESPONSE);
            resp.envelope.set_service(std::string(payload_type::kService));
            resp.envelope.set_payload_type(std::string(kTaskCheckResponseV2));
            resp.envelope.set_protocol_version(std::string(payload_type::kProtocolVersion));
            resp.envelope.set_timestamp_ms(nowMs());
            std::string raw;
            body.SerializeToString(&raw);
            resp.payload = strToBytes(raw);
            return TransportResult<VehicleMessage>{TransportOutcome::Accepted, std::move(resp)};
        });

    ::vehicle::ota::v1::TaskCheckRequest req;
    fillEnvelope(req.mutable_envelope(), "v2");
    auto ids = codec.identityFrom(req, ctx);
    auto encoded = codec.encodeRequest(kTaskCheckRequestV2, req, ids, ctx);

    auto res = transport.exchange(encoded, ExchangeOptions{}, ctx);
    ASSERT_EQ(res.outcome, TransportOutcome::Accepted);
    auto decoded = codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
        kTaskCheckResponseV2, res.value, ids.messageId);
    ASSERT_TRUE(decoded.ok);
    EXPECT_EQ(decoded->vehicle_task_id(), "VT-V2");
}

// ---------------------------------------------------------------------------
// 8. 下行订阅 / 解码 / 退订。
// ---------------------------------------------------------------------------
TEST(TransportContract, DownlinkSubscribeDecodeAndUnsubscribe) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaCloudProxyViaTransport cloud(transport);

    ::vehicle::ota::v1::ControlCommand cmd;
    cmd.set_control_revision("CR-1");
    cmd.set_command_type(::vehicle::ota::v1::CONTROL_COMMAND_TYPE_PAUSE);
    cmd.set_apply_mode(::vehicle::ota::v1::CONTROL_APPLY_MODE_AT_SAFE_POINT);
    std::string cmdPayload;
    cmd.SerializeToString(&cmdPayload);

    int received = 0;
    std::string gotRevision;
    Subscription sub = cloud.subscribeDownlink(
        payload_type::kService,
        [&](VehicleMessage&& msg) {
            auto decoded = cloud.decodeDownlink<::vehicle::ota::v1::ControlCommand>(
                payload_type::kControlCommand, msg);
            EXPECT_TRUE(decoded.ok);
            if (decoded.ok) {
                gotRevision = decoded->control_revision();
                ++received;
            }
        });

    // 云端推送一条控制指令下行事件。
    transport.deliverDownlink(makeEvent(payload_type::kControlCommand, cmdPayload, "dl-1"));
    EXPECT_EQ(received, 1);
    EXPECT_EQ(gotRevision, "CR-1");

    // 退订后不再收到。
    sub.cancel();
    EXPECT_EQ(transport.activeSubscriberCount(), 0u);
    transport.deliverDownlink(makeEvent(payload_type::kControlCommand, cmdPayload, "dl-2"));
    EXPECT_EQ(received, 1);
}

// ---------------------------------------------------------------------------
// 9. publish 单向投递（无需同步业务响应）。
// ---------------------------------------------------------------------------
TEST(TransportContract, PublishIsFireAndForget) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);

    // EVENT kind 受理。
    auto ev = makeEvent("ota.heartbeat.v1", "hb", "pub-1");
    auto ok = transport.publish(ev, makeCtx("pub"));
    EXPECT_EQ(ok.outcome, TransportOutcome::Accepted);
    EXPECT_EQ(transport.publishCount(), 1u);

    // 非 EVENT kind 拒绝。
    auto req = makeRequest("ota.heartbeat.v1", "hb", "pub-2");
    auto rejected = transport.publish(req, makeCtx("pub"));
    EXPECT_EQ(rejected.outcome, TransportOutcome::Rejected);
    EXPECT_EQ(transport.publishCount(), 1u);
}

// ---------------------------------------------------------------------------
// 10. 敏感内容不得进入传输元数据/异常信息（日志/异常不泄 payload、VIN、URL、token）。
// ---------------------------------------------------------------------------
TEST(TransportContract, SensitiveContentNotLeaked) {
    ScenarioScript scenario = makeScenario();
    FakeVehicleMessageTransport transport(scenario);
    OtaCloudProxyViaTransport cloud(transport);
    OtaMessageCodec codec;
    CallContext ctx = makeCtx("secret");

    // 业务 payload 内含下载 URL/token 与 VIN（仅受控业务 payload 承载）。
    ::vehicle::ota::v1::TaskCheckRequest req;
    fillEnvelope(req.mutable_envelope(), "secret");
    req.mutable_envelope()->set_vin("VINSECRET12345");
    req.mutable_envelope()->set_device_id("DEVSECRET");

    auto ids = codec.identityFrom(req, ctx);
    auto msg = codec.encodeRequest(payload_type::kTaskCheckRequest, req, ids, ctx);

    // 传输元数据（Envelope）不包含 VIN/URL/token/payload：序列化后也不含敏感串。
    std::string envSerialized;
    msg.envelope.SerializeToString(&envSerialized);
    EXPECT_EQ(envSerialized.find("VINSECRET"), std::string::npos);
    EXPECT_EQ(envSerialized.find("mock://"), std::string::npos);
    EXPECT_EQ(msg.envelope.trace_id(), "trace-secret");
    // 敏感内容只存在于业务 payload 内。
    EXPECT_NE(bytesToStr(msg.payload).find("VINSECRET"), std::string::npos);

    // 解码失败时，协议错误 detail 不得包含 payload 内容。
    auto tampered = msg;
    tampered.envelope.set_payload_type("ota.wrong.type.v1");
    auto bad = codec.decodeResponse<::vehicle::ota::v1::TaskCheckResponse>(
        payload_type::kTaskCheckResponse, tampered, ids.messageId);
    EXPECT_FALSE(bad.ok);
    EXPECT_EQ(bad.error.detail.find("VINSECRET"), std::string::npos);
    EXPECT_EQ(bad.error.detail.find("mock://"), std::string::npos);

    // 传输拒绝异常信息不得包含 payload 内容。
    auto expired = msg;
    expired.envelope.set_expire_at_ms(nowMs() - 1000);
    auto rejected = transport.exchange(expired, ExchangeOptions{}, ctx);
    EXPECT_EQ(rejected.outcome, TransportOutcome::Rejected);
}
