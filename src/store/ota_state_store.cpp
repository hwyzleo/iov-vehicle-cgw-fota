// =============================================================================
// src/store/ota_state_store.cpp
// CGW-FOTA 车云 OTA 状态存储封装实现 (CGW-FOTA-DSN-CR-009 §13.3)
// =============================================================================

#include "cgw/fota/store/ota_state_store.hpp"
#include "cgw/fota/store/fota_state_serializer.hpp"  // StateDecodeError

#include "fota_log_adapter.h"

namespace cgw_fota {
namespace store {
namespace ota {

namespace {
void logStoreError(const char* event, const std::string& key,
                   const std::string& errorCode, const std::string& stage = "") {
    FotaLogAdapter::store().error(
        event, "OTA store operation failed",
        {flog::f_str("key", key),
         flog::f_str("error_code", errorCode),
         flog::f_str("stage", stage)});
}
} // namespace

OtaStateStore::OtaStateStore(cgw::fw::store::Store store)
    : store_(std::move(store)) {}

// ---------------------------------------------------------------------------
// 通用 load/save/remove（内部链接，避免模板实例化）
// ---------------------------------------------------------------------------
namespace {

// load: 缺失(CGW-FW-0105) -> nullopt；解码失败 -> StateDecodeError(fail-closed)
template <typename Rec, typename DecodeFn>
std::optional<Rec> doLoad(const cgw::fw::store::Store& s, const char* key,
                          DecodeFn decode) {
    if (!s.has(key)) return std::nullopt;
    std::string bytes;
    try {
        bytes = s.load<std::string>(key);
    } catch (const cgw::fw::store::StoreException& e) {
        if (e.code == "CGW-FW-0105") return std::nullopt;
        logStoreError("fota.ota.store.load_failed", key, e.code, e.stage);
        throw;
    }
    try {
        return decode(bytes);
    } catch (const StateDecodeError& e) {
        logStoreError("fota.ota.store.decode_failed", key, "CGW-FW-0104", "decode");
        throw;
    }
}

template <typename Rec, typename EncodeFn>
void doSave(cgw::fw::store::Store& s, const char* key, const Rec& r, EncodeFn encode) {
    std::string bytes;
    try {
        bytes = encode(r);
    } catch (const std::exception&) {
        logStoreError("fota.ota.store.encode_failed", key, "CGW-FW-0104", "encode");
        throw;
    }
    try {
        s.save<std::string>(key, bytes);
    } catch (const cgw::fw::store::StoreException& e) {
        logStoreError("fota.ota.store.save_failed", key, e.code, e.stage);
        throw;
    }
}

void doRemove(cgw::fw::store::Store& s, const char* key) {
    try {
        s.remove(key);
    } catch (const cgw::fw::store::StoreException& e) {
        logStoreError("fota.ota.store.remove_failed", key, e.code, e.stage);
        throw;
    }
}

} // namespace

// ===========================================================================
// ota.vehicle_task
// ===========================================================================
std::optional<OtaVehicleTaskRecord> OtaStateStore::loadVehicleTask() {
    return doLoad<OtaVehicleTaskRecord>(store_, keys::VEHICLE_TASK, decodeVehicleTask);
}
void OtaStateStore::saveVehicleTask(const OtaVehicleTaskRecord& r) {
    doSave(store_, keys::VEHICLE_TASK, r, encodeVehicleTask);
}
void OtaStateStore::removeVehicleTask() { doRemove(store_, keys::VEHICLE_TASK); }

// ===========================================================================
// ota.inventory
// ===========================================================================
std::optional<OtaInventoryRecord> OtaStateStore::loadInventory() {
    return doLoad<OtaInventoryRecord>(store_, keys::INVENTORY, decodeInventory);
}
void OtaStateStore::saveInventory(const OtaInventoryRecord& r) {
    doSave(store_, keys::INVENTORY, r, encodeInventory);
}
void OtaStateStore::removeInventory() { doRemove(store_, keys::INVENTORY); }

// ===========================================================================
// ota.consent
// ===========================================================================
std::optional<OtaConsentRecord> OtaStateStore::loadConsent() {
    return doLoad<OtaConsentRecord>(store_, keys::CONSENT, decodeConsent);
}
void OtaStateStore::saveConsent(const OtaConsentRecord& r) {
    doSave(store_, keys::CONSENT, r, encodeConsent);
}
void OtaStateStore::removeConsent() { doRemove(store_, keys::CONSENT); }

// ===========================================================================
// ota.downloads
// ===========================================================================
std::optional<OtaDownloadsRecord> OtaStateStore::loadDownloads() {
    return doLoad<OtaDownloadsRecord>(store_, keys::DOWNLOADS, decodeDownloads);
}
void OtaStateStore::saveDownloads(const OtaDownloadsRecord& r) {
    doSave(store_, keys::DOWNLOADS, r, encodeDownloads);
}
void OtaStateStore::removeDownloads() { doRemove(store_, keys::DOWNLOADS); }

// ===========================================================================
// ota.execution
// ===========================================================================
std::optional<OtaExecutionRecord> OtaStateStore::loadExecution() {
    return doLoad<OtaExecutionRecord>(store_, keys::EXECUTION, decodeExecution);
}
void OtaStateStore::saveExecution(const OtaExecutionRecord& r) {
    doSave(store_, keys::EXECUTION, r, encodeExecution);
}
void OtaStateStore::removeExecution() { doRemove(store_, keys::EXECUTION); }

// ===========================================================================
// ota.event_outbox
// ===========================================================================
std::optional<OtaEventOutboxRecord> OtaStateStore::loadEventOutbox() {
    return doLoad<OtaEventOutboxRecord>(store_, keys::EVENT_OUTBOX, decodeEventOutbox);
}
void OtaStateStore::saveEventOutbox(const OtaEventOutboxRecord& r) {
    doSave(store_, keys::EVENT_OUTBOX, r, encodeEventOutbox);
}
void OtaStateStore::removeEventOutbox() { doRemove(store_, keys::EVENT_OUTBOX); }

// ===========================================================================
// ota.controls
// ===========================================================================
std::optional<OtaControlsRecord> OtaStateStore::loadControls() {
    return doLoad<OtaControlsRecord>(store_, keys::CONTROLS, decodeControls);
}
void OtaStateStore::saveControls(const OtaControlsRecord& r) {
    doSave(store_, keys::CONTROLS, r, encodeControls);
}
void OtaStateStore::removeControls() { doRemove(store_, keys::CONTROLS); }

// ===========================================================================
// ota.policy
// ===========================================================================
std::optional<OtaPolicyRecord> OtaStateStore::loadPolicy() {
    return doLoad<OtaPolicyRecord>(store_, keys::POLICY, decodePolicy);
}
void OtaStateStore::savePolicy(const OtaPolicyRecord& r) {
    doSave(store_, keys::POLICY, r, encodePolicy);
}
void OtaStateStore::removePolicy() { doRemove(store_, keys::POLICY); }

// ===========================================================================
// ota.log_jobs
// ===========================================================================
std::optional<OtaLogJobsRecord> OtaStateStore::loadLogJobs() {
    return doLoad<OtaLogJobsRecord>(store_, keys::LOG_JOBS, decodeLogJobs);
}
void OtaStateStore::saveLogJobs(const OtaLogJobsRecord& r) {
    doSave(store_, keys::LOG_JOBS, r, encodeLogJobs);
}
void OtaStateStore::removeLogJobs() { doRemove(store_, keys::LOG_JOBS); }

} // namespace ota
} // namespace store
} // namespace cgw_fota
