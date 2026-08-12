#pragma once

// =============================================================================
// include/cgw/fota/store/ota_state.hpp
// CGW-FOTA 车云 OTA 持久化状态模型 (CGW-FOTA-DSN-CR-009 §13.3 持久化与恢复)
// =============================================================================
// 定义通过 cgw-framework-store 持久化的 9 类版本化 OTA 记录：
//   ota.vehicle_task - 当前任务、taskRevision、时间窗、策略、包清单、本地处置
//   ota.inventory    - FULL 清单、inventoryRevision、算法、digest
//   ota.consent      - 条款身份、scope digest、receipt、有效期、权威状态
//   ota.downloads    - 各包 packageRevision/ETag/offset/凭证/校验/stageResult
//   ota.execution    - execution/attempt/permit/control/冻结策略/manifest/阶段/checkpoint
//   ota.event_outbox - 事件 ID/摘要/payload/发送状态；连续水位确认后清理
//   ota.controls     - 指令/回执序号/状态/原因
//   ota.policy       - 本地偏好版本/云端版本/有效策略
//   ota.log_jobs     - 采集范围/对象键/摘要/上传状态
//
// 复杂 proto 消息（FrozenTaskSnapshot/InventoryInfo/ConsentReceipt/InstallCheckpoint/
// TaskPolicy/EffectivePolicy）以 proto-binary hex 内嵌于 JSON，避免重复字段序列化，
// 并保持与既有 FTSE envelope+JSON 模式一致。每条记录含 schemaVersion、
// writer/minReader、业务摘要和完整性元数据；未知新版本 fail-closed。
//
// 多条目 key（downloads/event_outbox/controls/log_jobs）聚合为单记录，利用 store
// 单 key 原子写保证类别内一致性；容量门禁由 event_outbox_max 等配置约束。
// 大规模分片/独立 TTL 为后续 CR（§16 待细化）。
// =============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace cgw_fota {
namespace store {
namespace ota {

using Timestamp = std::int64_t;

// ============================================================
// Store keys (CGW-FOTA-DSN-CR-009 §13.3)
// ============================================================
namespace keys {
constexpr const char* VEHICLE_TASK = "ota.vehicle_task";
constexpr const char* INVENTORY    = "ota.inventory";
constexpr const char* CONSENT      = "ota.consent";
constexpr const char* DOWNLOADS    = "ota.downloads";
constexpr const char* EXECUTION    = "ota.execution";
constexpr const char* EVENT_OUTBOX = "ota.event_outbox";
constexpr const char* CONTROLS     = "ota.controls";
constexpr const char* POLICY       = "ota.policy";
constexpr const char* LOG_JOBS     = "ota.log_jobs";
} // namespace keys

// ============================================================
// Schema versions（业务级，envelope 内）
// ============================================================
namespace schema {
constexpr std::uint32_t VEHICLE_TASK_VERSION = 1;
constexpr std::uint32_t INVENTORY_VERSION    = 1;
constexpr std::uint32_t CONSENT_VERSION      = 1;
constexpr std::uint32_t DOWNLOADS_VERSION    = 1;
constexpr std::uint32_t EXECUTION_VERSION    = 1;
constexpr std::uint32_t EVENT_OUTBOX_VERSION = 1;
constexpr std::uint32_t CONTROLS_VERSION     = 1;
constexpr std::uint32_t POLICY_VERSION       = 1;
constexpr std::uint32_t LOG_JOBS_VERSION     = 1;

constexpr std::uint32_t MIN_READER_VERSION = 0;
constexpr std::uint32_t WRITER_VERSION     = 1;
} // namespace schema

// ============================================================
// ota.vehicle_task
// ============================================================
struct OtaVehicleTaskRecord {
    std::uint32_t schemaVersion = schema::VEHICLE_TASK_VERSION;
    std::string   vehicleTaskId;
    std::string   taskRevision;
    std::string   targetBaselineId;
    std::string   vehicleTaskState;        // VehicleTaskState 名
    Timestamp     frozenAtMs = 0;
    std::string   frozenSnapshotPbHex;     // FrozenTaskSnapshot proto binary hex
    std::string   localDispositionResult;  // 本地旧任务处置结果
    bool          superseded = false;
};

// ============================================================
// ota.inventory
// ============================================================
struct OtaInventoryRecord {
    std::uint32_t schemaVersion = schema::INVENTORY_VERSION;
    std::string   mode;                    // FULL / DIGEST
    std::string   inventoryRevision;
    std::string   algorithm;
    std::string   ecuListDigest;
    Timestamp     collectedAtMs = 0;
    std::string   inventoryPbHex;          // InventoryInfo proto binary hex（FULL）
    bool          fullRequired = false;    // 云端要求下次 FULL
};

// ============================================================
// ota.consent
// ============================================================
struct OtaConsentRecord {
    std::uint32_t schemaVersion = schema::CONSENT_VERSION;
    std::string   vehicleTaskId;
    std::string   effectiveStatus;         // ConsentStatus 名
    std::string   receiptId;
    Timestamp     receiptExpiresAtMs = 0;
    std::string   consentReceiptPbHex;     // ConsentReceipt proto binary hex
    std::string   termsId;
    std::string   termsVersion;
};

// ============================================================
// ota.downloads（聚合各包）
// ============================================================
struct OtaDownloadEntry {
    std::string   packageId;
    std::string   packageRevision;
    std::string   etag;
    std::int64_t  offset = 0;              // STORED_OBJECT 字节偏移
    Timestamp     credentialExpiresAtMs = 0;
    std::string   verifyStatus;            // PENDING / SUCCEEDED / FAILED
    std::string   stageResultId;
    std::string   stageResultDigest;
    bool          ready = false;
};

struct OtaDownloadsRecord {
    std::uint32_t               schemaVersion = schema::DOWNLOADS_VERSION;
    std::vector<OtaDownloadEntry> entries;
    std::string                 packageManifestDigestHex;
    std::string                 packageManifestAlgorithm;
    bool                        allReady = false;
};

// ============================================================
// ota.execution
// ============================================================
struct OtaExecutionRecord {
    std::uint32_t schemaVersion = schema::EXECUTION_VERSION;
    std::string   vehicleTaskId;
    std::string   executionId;
    std::uint32_t attemptNo = 0;
    std::string   permitId;
    std::string   permitToken;
    std::string   controlRevision;
    Timestamp     validUntilMs = 0;        // 仅约束进入 INSTALL_STARTED
    std::string   executionState;          // ExecutionState 名
    std::string   stage;                   // ExecutionStage 名
    std::uint32_t progressPercent = 0;
    std::string   checkpointPbHex;         // InstallCheckpoint proto binary hex
    std::uint64_t acceptedSequenceNo = 0;
    std::uint64_t nextSequenceNo = 0;
    std::uint64_t finalSequenceNo = 0;
    std::string   resultStatus;            // ExecutionState 名（终态）
    std::string   offlinePolicyPbHex;      // TaskPolicy proto binary hex
    std::string   timeoutPolicyPbHex;      // TaskPolicy proto binary hex
};

// ============================================================
// ota.event_outbox（聚合 bounded 事件）
// ============================================================
struct OtaEventOutboxEntry {
    std::uint64_t sequenceNo = 0;
    std::string   eventId;
    std::string   eventDigest;
    std::string   stage;
    std::uint32_t progressPercent = 0;
    std::string   result;
    Timestamp     timestampMs = 0;
    std::string   payloadSummary;
    std::string   sendStatus;              // PENDING / SENT / ACKED
    std::string   eventPbHex;              // ExecutionEvent proto binary hex（重发用）
};

struct OtaEventOutboxRecord {
    std::uint32_t                 schemaVersion = schema::EVENT_OUTBOX_VERSION;
    std::uint64_t                 nextSequenceNo = 1;
    std::uint64_t                 acceptedSequenceNo = 0;
    std::vector<OtaEventOutboxEntry> entries;
};

// ============================================================
// ota.controls（聚合 bounded 控制）
// ============================================================
struct OtaControlEntry {
    std::string   controlRevision;
    std::string   commandType;
    std::string   applyMode;
    Timestamp     expiresAtMs = 0;
    std::string   reason;
    std::string   ackStatus;               // ControlAckStatus 名
    std::uint64_t ackSequenceNo = 0;
    Timestamp     appliedAtMs = 0;
};

struct OtaControlsRecord {
    std::uint32_t               schemaVersion = schema::CONTROLS_VERSION;
    std::string                 lastAppliedRevision;
    std::vector<OtaControlEntry> entries;
};

// ============================================================
// ota.policy
// ============================================================
struct OtaPolicyRecord {
    std::uint32_t schemaVersion = schema::POLICY_VERSION;
    std::string   basePreferenceVersion;
    std::string   preferenceVersion;
    std::string   effectivePolicyPbHex;    // EffectivePolicy proto binary hex
    bool          conflict = false;
};

// ============================================================
// ota.log_jobs（聚合 bounded 日志任务）
// ============================================================
struct OtaLogJobEntry {
    std::string   logRequestId;
    std::string   objectKey;
    std::string   digestHex;
    std::int64_t  sizeBytes = 0;
    std::string   status;                  // PENDING / UPLOADED / FAILED
    Timestamp     completedAtMs = 0;
};

struct OtaLogJobsRecord {
    std::uint32_t             schemaVersion = schema::LOG_JOBS_VERSION;
    std::vector<OtaLogJobEntry> entries;
};

} // namespace ota
} // namespace store
} // namespace cgw_fota
