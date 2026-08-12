#pragma once

// =============================================================================
// include/cgw/fota/store/ota_state_store.hpp
// CGW-FOTA 车云 OTA 状态存储封装 (CGW-FOTA-DSN-CR-009 §13.3)
// =============================================================================
// 包装 cgw-framework-store，为 9 类 OTA key 提供类型化 load/save/remove。
// 与 FotaStateStore 共享同一 service "fota" 存储（通过 underlyingStore()），
// 避免重复 open 造成锁冲突。同步 durable 写入；未知新版本 fail-closed。
//
// 不提供跨 key ACID；一致性由严格写入顺序、稳定 taskRevision/executionId/
// sequenceNo/controlRevision/idempotencyKey 与启动对账保证。
// =============================================================================

#include "cgw/fota/store/ota_state.hpp"
#include "cgw/fota/store/ota_state_serializer.hpp"

#include "store.h"          // cgw::fw::store::Store
#include "store_types.h"    // StoreException

#include <optional>

namespace cgw_fota {
namespace store {
namespace ota {

class OtaStateStore {
public:
    // 从已打开的 framework Store 构造（与 FotaStateStore 共享同一 service 存储）。
    explicit OtaStateStore(cgw::fw::store::Store store);

    // ---- ota.vehicle_task ----
    std::optional<OtaVehicleTaskRecord> loadVehicleTask();
    void saveVehicleTask(const OtaVehicleTaskRecord& r);
    void removeVehicleTask();

    // ---- ota.inventory ----
    std::optional<OtaInventoryRecord> loadInventory();
    void saveInventory(const OtaInventoryRecord& r);
    void removeInventory();

    // ---- ota.consent ----
    std::optional<OtaConsentRecord> loadConsent();
    void saveConsent(const OtaConsentRecord& r);
    void removeConsent();

    // ---- ota.downloads ----
    std::optional<OtaDownloadsRecord> loadDownloads();
    void saveDownloads(const OtaDownloadsRecord& r);
    void removeDownloads();

    // ---- ota.execution ----
    std::optional<OtaExecutionRecord> loadExecution();
    void saveExecution(const OtaExecutionRecord& r);
    void removeExecution();

    // ---- ota.event_outbox ----
    std::optional<OtaEventOutboxRecord> loadEventOutbox();
    void saveEventOutbox(const OtaEventOutboxRecord& r);
    void removeEventOutbox();

    // ---- ota.controls ----
    std::optional<OtaControlsRecord> loadControls();
    void saveControls(const OtaControlsRecord& r);
    void removeControls();

    // ---- ota.policy ----
    std::optional<OtaPolicyRecord> loadPolicy();
    void savePolicy(const OtaPolicyRecord& r);
    void removePolicy();

    // ---- ota.log_jobs ----
    std::optional<OtaLogJobsRecord> loadLogJobs();
    void saveLogJobs(const OtaLogJobsRecord& r);
    void removeLogJobs();

    // 同步 flush（no-op for Synchronous）。
    void flush() { store_.flush(); }

private:
    cgw::fw::store::Store store_;
};

} // namespace ota
} // namespace store
} // namespace cgw_fota
