// =============================================================================
// src/store/ota_state_serializer.cpp
// CGW-FOTA OTA 状态序列化器实现 (CGW-FOTA-DSN-CR-009 §13.3)
// =============================================================================

#include "cgw/fota/store/ota_state_serializer.hpp"
#include "cgw/fota/store/fota_state_serializer.hpp"  // FTSE envelope 复用

#include <nlohmann/json.hpp>

#include <cgw/fw/hash/hex.hpp>  // CGW-FOTA-DSN-CR-006: 可逆 hex 编解码经 framework

#include <stdexcept>
#include <string_view>

namespace cgw_fota {
namespace store {
namespace ota {

using json = nlohmann::json;
using cgw_fota::store::encodeEnvelope;
using cgw_fota::store::parseEnvelopeHeader;
using cgw_fota::store::extractPayload;
using cgw_fota::store::StateDecodeError;

// ===========================================================================
// proto-binary <-> hex
// ---------------------------------------------------------------------------
// CGW-FOTA-DSN-CR-006: FOTA 仓内不实现私有 hex 编码器。proto-binary 的可逆
// hex 编解码经 cgw-framework-hash 通用工具（<cgw/fw/hash/hex.hpp>）；摘要 hex
// 仍由 sha256_hex 产出。此处仅做错误翻译。
// ===========================================================================
std::string protoBinaryToHex(const std::string& binary) {
    return cgw::fw::hash::bytesToHex(binary);
}

std::string hexToProtoBinary(const std::string& hex) {
    try {
        return cgw::fw::hash::hexToBytes(hex);
    } catch (const cgw::fw::hash::HexException& e) {
        // CGW-FW-0410=odd length, CGW-FW-0411=invalid char (carries index)
        if (e.position() != std::string_view::npos) {
            throw StateDecodeError(std::string("proto hex decode failed: ") +
                                   e.code() + " at index " +
                                   std::to_string(e.position()));
        }
        throw StateDecodeError(std::string("proto hex decode failed: ") + e.code());
    }
}

namespace {

// 通用 envelope+JSON 解码：解析 envelope、校验 schemaVersion、返回 JSON。
// 未知新版本 fail-closed。
json decodeEnvelopeJson(const std::string& bytes, std::uint32_t expectedVersion,
                        const char* tag) {
    EnvelopeHeader h = parseEnvelopeHeader(bytes);
    if (h.schemaVersion > expectedVersion) {
        throw StateDecodeError(std::string(tag) + " unknown newer schemaVersion: " +
                               std::to_string(h.schemaVersion));
    }
    std::string payload = extractPayload(bytes, h);
    if (h.schemaVersion < expectedVersion) {
        // 当前所有 OTA schema 均为 v1，无旧版本可迁移；低于 v1 视为未知。
        throw StateDecodeError(std::string(tag) + " unsupported older schemaVersion: " +
                               std::to_string(h.schemaVersion));
    }
    try {
        return json::parse(payload);
    } catch (const json::exception& e) {
        throw StateDecodeError(std::string(tag) + " json parse failed: " + e.what());
    }
}

std::string getStr(const json& j, const char* key) {
    return j.at(key).get<std::string>();
}
std::string getStrOpt(const json& j, const char* key, const char* dflt) {
    return j.value(key, std::string(dflt));
}

} // namespace

// ===========================================================================
// ota.vehicle_task
// ===========================================================================
std::string encodeVehicleTask(const OtaVehicleTaskRecord& r) {
    json j = json{
        {"schemaVersion", r.schemaVersion},
        {"vehicleTaskId", r.vehicleTaskId},
        {"taskRevision", r.taskRevision},
        {"targetBaselineId", r.targetBaselineId},
        {"vehicleTaskState", r.vehicleTaskState},
        {"frozenAtMs", r.frozenAtMs},
        {"frozenSnapshotPbHex", r.frozenSnapshotPbHex},
        {"localDispositionResult", r.localDispositionResult},
        {"superseded", r.superseded}};
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaVehicleTaskRecord decodeVehicleTask(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::VEHICLE_TASK_VERSION, "vehicle_task");
    OtaVehicleTaskRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::VEHICLE_TASK_VERSION);
    r.vehicleTaskId = getStr(j, "vehicleTaskId");
    r.taskRevision = getStr(j, "taskRevision");
    r.targetBaselineId = getStr(j, "targetBaselineId");
    r.vehicleTaskState = getStr(j, "vehicleTaskState");
    r.frozenAtMs = j.at("frozenAtMs").get<Timestamp>();
    r.frozenSnapshotPbHex = getStrOpt(j, "frozenSnapshotPbHex", "");
    r.localDispositionResult = getStrOpt(j, "localDispositionResult", "");
    r.superseded = j.value("superseded", false);
    return r;
}

// ===========================================================================
// ota.inventory
// ===========================================================================
std::string encodeInventory(const OtaInventoryRecord& r) {
    json j = json{
        {"schemaVersion", r.schemaVersion},
        {"mode", r.mode},
        {"inventoryRevision", r.inventoryRevision},
        {"algorithm", r.algorithm},
        {"ecuListDigest", r.ecuListDigest},
        {"collectedAtMs", r.collectedAtMs},
        {"inventoryPbHex", r.inventoryPbHex},
        {"fullRequired", r.fullRequired}};
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaInventoryRecord decodeInventory(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::INVENTORY_VERSION, "inventory");
    OtaInventoryRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::INVENTORY_VERSION);
    r.mode = getStr(j, "mode");
    r.inventoryRevision = getStr(j, "inventoryRevision");
    r.algorithm = getStr(j, "algorithm");
    r.ecuListDigest = getStr(j, "ecuListDigest");
    r.collectedAtMs = j.at("collectedAtMs").get<Timestamp>();
    r.inventoryPbHex = getStrOpt(j, "inventoryPbHex", "");
    r.fullRequired = j.value("fullRequired", false);
    return r;
}

// ===========================================================================
// ota.consent
// ===========================================================================
std::string encodeConsent(const OtaConsentRecord& r) {
    json j = json{
        {"schemaVersion", r.schemaVersion},
        {"vehicleTaskId", r.vehicleTaskId},
        {"effectiveStatus", r.effectiveStatus},
        {"receiptId", r.receiptId},
        {"receiptExpiresAtMs", r.receiptExpiresAtMs},
        {"consentReceiptPbHex", r.consentReceiptPbHex},
        {"termsId", r.termsId},
        {"termsVersion", r.termsVersion}};
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaConsentRecord decodeConsent(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::CONSENT_VERSION, "consent");
    OtaConsentRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::CONSENT_VERSION);
    r.vehicleTaskId = getStr(j, "vehicleTaskId");
    r.effectiveStatus = getStr(j, "effectiveStatus");
    r.receiptId = getStr(j, "receiptId");
    r.receiptExpiresAtMs = j.at("receiptExpiresAtMs").get<Timestamp>();
    r.consentReceiptPbHex = getStrOpt(j, "consentReceiptPbHex", "");
    r.termsId = getStrOpt(j, "termsId", "");
    r.termsVersion = getStrOpt(j, "termsVersion", "");
    return r;
}

// ===========================================================================
// ota.downloads
// ===========================================================================
json downloadEntryToJson(const OtaDownloadEntry& e) {
    return json{{"packageId", e.packageId},
                {"packageRevision", e.packageRevision},
                {"etag", e.etag},
                {"offset", e.offset},
                {"credentialExpiresAtMs", e.credentialExpiresAtMs},
                {"verifyStatus", e.verifyStatus},
                {"stageResultId", e.stageResultId},
                {"stageResultDigest", e.stageResultDigest},
                {"ready", e.ready}};
}

OtaDownloadEntry downloadEntryFromJson(const json& j) {
    OtaDownloadEntry e;
    e.packageId = j.at("packageId").get<std::string>();
    e.packageRevision = j.at("packageRevision").get<std::string>();
    e.etag = j.at("etag").get<std::string>();
    e.offset = j.at("offset").get<std::int64_t>();
    e.credentialExpiresAtMs = j.at("credentialExpiresAtMs").get<Timestamp>();
    e.verifyStatus = j.at("verifyStatus").get<std::string>();
    e.stageResultId = j.at("stageResultId").get<std::string>();
    e.stageResultDigest = j.at("stageResultDigest").get<std::string>();
    e.ready = j.value("ready", false);
    return e;
}

std::string encodeDownloads(const OtaDownloadsRecord& r) {
    json j = json{{"schemaVersion", r.schemaVersion},
                  {"entries", json::array()},
                  {"packageManifestDigestHex", r.packageManifestDigestHex},
                  {"packageManifestAlgorithm", r.packageManifestAlgorithm},
                  {"allReady", r.allReady}};
    for (const auto& e : r.entries) j["entries"].push_back(downloadEntryToJson(e));
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaDownloadsRecord decodeDownloads(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::DOWNLOADS_VERSION, "downloads");
    OtaDownloadsRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::DOWNLOADS_VERSION);
    for (const auto& ej : j.at("entries")) r.entries.push_back(downloadEntryFromJson(ej));
    r.packageManifestDigestHex = getStrOpt(j, "packageManifestDigestHex", "");
    r.packageManifestAlgorithm = getStrOpt(j, "packageManifestAlgorithm", "");
    r.allReady = j.value("allReady", false);
    return r;
}

// ===========================================================================
// ota.execution
// ===========================================================================
std::string encodeExecution(const OtaExecutionRecord& r) {
    json j = json{
        {"schemaVersion", r.schemaVersion},
        {"vehicleTaskId", r.vehicleTaskId},
        {"executionId", r.executionId},
        {"attemptNo", r.attemptNo},
        {"permitId", r.permitId},
        {"permitToken", r.permitToken},
        {"controlRevision", r.controlRevision},
        {"validUntilMs", r.validUntilMs},
        {"executionState", r.executionState},
        {"stage", r.stage},
        {"progressPercent", r.progressPercent},
        {"checkpointPbHex", r.checkpointPbHex},
        {"acceptedSequenceNo", r.acceptedSequenceNo},
        {"nextSequenceNo", r.nextSequenceNo},
        {"finalSequenceNo", r.finalSequenceNo},
        {"resultStatus", r.resultStatus},
        {"offlinePolicyPbHex", r.offlinePolicyPbHex},
        {"timeoutPolicyPbHex", r.timeoutPolicyPbHex}};
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaExecutionRecord decodeExecution(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::EXECUTION_VERSION, "execution");
    OtaExecutionRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::EXECUTION_VERSION);
    r.vehicleTaskId = getStr(j, "vehicleTaskId");
    r.executionId = getStr(j, "executionId");
    r.attemptNo = j.at("attemptNo").get<std::uint32_t>();
    r.permitId = getStr(j, "permitId");
    r.permitToken = getStr(j, "permitToken");
    r.controlRevision = getStr(j, "controlRevision");
    r.validUntilMs = j.at("validUntilMs").get<Timestamp>();
    r.executionState = getStr(j, "executionState");
    r.stage = getStrOpt(j, "stage", "");
    r.progressPercent = j.value("progressPercent", std::uint32_t(0));
    r.checkpointPbHex = getStrOpt(j, "checkpointPbHex", "");
    r.acceptedSequenceNo = j.at("acceptedSequenceNo").get<std::uint64_t>();
    r.nextSequenceNo = j.at("nextSequenceNo").get<std::uint64_t>();
    r.finalSequenceNo = j.value("finalSequenceNo", std::uint64_t(0));
    r.resultStatus = getStrOpt(j, "resultStatus", "");
    r.offlinePolicyPbHex = getStrOpt(j, "offlinePolicyPbHex", "");
    r.timeoutPolicyPbHex = getStrOpt(j, "timeoutPolicyPbHex", "");
    return r;
}

// ===========================================================================
// ota.event_outbox
// ===========================================================================
json eventOutboxEntryToJson(const OtaEventOutboxEntry& e) {
    return json{{"sequenceNo", e.sequenceNo},
                {"eventId", e.eventId},
                {"eventDigest", e.eventDigest},
                {"stage", e.stage},
                {"progressPercent", e.progressPercent},
                {"result", e.result},
                {"timestampMs", e.timestampMs},
                {"payloadSummary", e.payloadSummary},
                {"sendStatus", e.sendStatus},
                {"eventPbHex", e.eventPbHex}};
}

OtaEventOutboxEntry eventOutboxEntryFromJson(const json& j) {
    OtaEventOutboxEntry e;
    e.sequenceNo = j.at("sequenceNo").get<std::uint64_t>();
    e.eventId = j.at("eventId").get<std::string>();
    e.eventDigest = j.at("eventDigest").get<std::string>();
    e.stage = j.at("stage").get<std::string>();
    e.progressPercent = j.value("progressPercent", std::uint32_t(0));
    e.result = j.at("result").get<std::string>();
    e.timestampMs = j.at("timestampMs").get<Timestamp>();
    e.payloadSummary = j.at("payloadSummary").get<std::string>();
    e.sendStatus = j.at("sendStatus").get<std::string>();
    e.eventPbHex = j.at("eventPbHex").get<std::string>();
    return e;
}

std::string encodeEventOutbox(const OtaEventOutboxRecord& r) {
    json j = json{{"schemaVersion", r.schemaVersion},
                  {"nextSequenceNo", r.nextSequenceNo},
                  {"acceptedSequenceNo", r.acceptedSequenceNo},
                  {"entries", json::array()}};
    for (const auto& e : r.entries) j["entries"].push_back(eventOutboxEntryToJson(e));
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaEventOutboxRecord decodeEventOutbox(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::EVENT_OUTBOX_VERSION, "event_outbox");
    OtaEventOutboxRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::EVENT_OUTBOX_VERSION);
    r.nextSequenceNo = j.at("nextSequenceNo").get<std::uint64_t>();
    r.acceptedSequenceNo = j.at("acceptedSequenceNo").get<std::uint64_t>();
    for (const auto& ej : j.at("entries")) r.entries.push_back(eventOutboxEntryFromJson(ej));
    return r;
}

// ===========================================================================
// ota.controls
// ===========================================================================
json controlEntryToJson(const OtaControlEntry& e) {
    return json{{"controlRevision", e.controlRevision},
                {"commandType", e.commandType},
                {"applyMode", e.applyMode},
                {"expiresAtMs", e.expiresAtMs},
                {"reason", e.reason},
                {"ackStatus", e.ackStatus},
                {"ackSequenceNo", e.ackSequenceNo},
                {"appliedAtMs", e.appliedAtMs}};
}

OtaControlEntry controlEntryFromJson(const json& j) {
    OtaControlEntry e;
    e.controlRevision = j.at("controlRevision").get<std::string>();
    e.commandType = j.at("commandType").get<std::string>();
    e.applyMode = j.at("applyMode").get<std::string>();
    e.expiresAtMs = j.at("expiresAtMs").get<Timestamp>();
    e.reason = j.at("reason").get<std::string>();
    e.ackStatus = j.at("ackStatus").get<std::string>();
    e.ackSequenceNo = j.at("ackSequenceNo").get<std::uint64_t>();
    e.appliedAtMs = j.at("appliedAtMs").get<Timestamp>();
    return e;
}

std::string encodeControls(const OtaControlsRecord& r) {
    json j = json{{"schemaVersion", r.schemaVersion},
                  {"lastAppliedRevision", r.lastAppliedRevision},
                  {"entries", json::array()}};
    for (const auto& e : r.entries) j["entries"].push_back(controlEntryToJson(e));
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaControlsRecord decodeControls(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::CONTROLS_VERSION, "controls");
    OtaControlsRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::CONTROLS_VERSION);
    r.lastAppliedRevision = getStrOpt(j, "lastAppliedRevision", "");
    for (const auto& ej : j.at("entries")) r.entries.push_back(controlEntryFromJson(ej));
    return r;
}

// ===========================================================================
// ota.policy
// ===========================================================================
std::string encodePolicy(const OtaPolicyRecord& r) {
    json j = json{{"schemaVersion", r.schemaVersion},
                  {"basePreferenceVersion", r.basePreferenceVersion},
                  {"preferenceVersion", r.preferenceVersion},
                  {"effectivePolicyPbHex", r.effectivePolicyPbHex},
                  {"conflict", r.conflict}};
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaPolicyRecord decodePolicy(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::POLICY_VERSION, "policy");
    OtaPolicyRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::POLICY_VERSION);
    r.basePreferenceVersion = getStr(j, "basePreferenceVersion");
    r.preferenceVersion = getStr(j, "preferenceVersion");
    r.effectivePolicyPbHex = getStrOpt(j, "effectivePolicyPbHex", "");
    r.conflict = j.value("conflict", false);
    return r;
}

// ===========================================================================
// ota.log_jobs
// ===========================================================================
json logJobEntryToJson(const OtaLogJobEntry& e) {
    return json{{"logRequestId", e.logRequestId},
                {"objectKey", e.objectKey},
                {"digestHex", e.digestHex},
                {"sizeBytes", e.sizeBytes},
                {"status", e.status},
                {"completedAtMs", e.completedAtMs}};
}

OtaLogJobEntry logJobEntryFromJson(const json& j) {
    OtaLogJobEntry e;
    e.logRequestId = j.at("logRequestId").get<std::string>();
    e.objectKey = j.at("objectKey").get<std::string>();
    e.digestHex = j.at("digestHex").get<std::string>();
    e.sizeBytes = j.at("sizeBytes").get<std::int64_t>();
    e.status = j.at("status").get<std::string>();
    e.completedAtMs = j.at("completedAtMs").get<Timestamp>();
    return e;
}

std::string encodeLogJobs(const OtaLogJobsRecord& r) {
    json j = json{{"schemaVersion", r.schemaVersion}, {"entries", json::array()}};
    for (const auto& e : r.entries) j["entries"].push_back(logJobEntryToJson(e));
    return encodeEnvelope(r.schemaVersion, j.dump());
}

OtaLogJobsRecord decodeLogJobs(const std::string& bytes) {
    json j = decodeEnvelopeJson(bytes, schema::LOG_JOBS_VERSION, "log_jobs");
    OtaLogJobsRecord r;
    r.schemaVersion = j.value("schemaVersion", schema::LOG_JOBS_VERSION);
    for (const auto& ej : j.at("entries")) r.entries.push_back(logJobEntryFromJson(ej));
    return r;
}

} // namespace ota
} // namespace store
} // namespace cgw_fota
