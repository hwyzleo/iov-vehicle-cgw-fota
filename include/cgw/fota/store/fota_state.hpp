#pragma once

// =============================================================================
// include/cgw/fota/store/fota_state.hpp
// CGW-FOTA 运行状态模型 (CGW-FOTA-DSN-CR-005)
// =============================================================================
// 定义通过 cgw-framework-store 持久化的四类运行状态及其版本化 schema：
//   inventory.sequence     - 已分配最高 snapshotSeq（同步 durable，损坏禁报）
//   inventory.last_success - 最后成功快照（同步 durable，损坏禁用去重并重采集）
//   inventory.dedupe       - 有界去重窗口（同步，损坏清空逻辑窗口）
//   inventory.active_job   - 在途/重试检查点（同步 durable，按阶段保守恢复）
//
// 所有记录使用显式字段序列化（见 fota_state_serializer），不保存 C++ 内存布局。
// schemaVersion 为业务级版本，置于 FOTA envelope 内，由序列化器校验与迁移。
// 去重指纹算法由后续 hash CR 固化，本 CR 仅要求其格式版本化与持久化。
// =============================================================================

#include "data_models.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cgw_fota {
namespace store {

// Timestamp: milliseconds since Unix epoch (UTC). 序列化稳定，不依赖 C++ 时钟布局。
using Timestamp = std::int64_t;

// ============================================================
// Store keys (CGW-FOTA-DSN-CR-005 §Key 与状态模型)
// ============================================================
namespace keys {
constexpr const char* SEQUENCE     = "inventory.sequence";
constexpr const char* LAST_SUCCESS = "inventory.last_success";
constexpr const char* DEDUPE       = "inventory.dedupe";
constexpr const char* ACTIVE_JOB   = "inventory.active_job";
} // namespace keys

// ============================================================
// Schema versions (业务级，envelope 内)
// 当前版本为 1；v0 为可迁移旧格式（用于迁移链与 golden fixtures）。
// ============================================================
namespace schema {
constexpr std::uint32_t SEQUENCE_VERSION     = 1;
constexpr std::uint32_t LAST_SUCCESS_VERSION = 1;
constexpr std::uint32_t DEDUPE_VERSION       = 1;
constexpr std::uint32_t ACTIVE_JOB_VERSION   = 1;

// 本二进制可读的最低 envelope schemaVersion。低于此值需迁移；高于 CURRENT fail-closed。
constexpr std::uint32_t MIN_READER_VERSION = 0;   // 可迁移 v0
constexpr std::uint32_t WRITER_VERSION     = 1;
} // namespace schema

// ============================================================
// JobPhase - 任务检查点状态机 (CGW-FOTA-DSN-CR-005 §任务检查点与提交顺序)
// ============================================================
enum class JobPhase : std::uint8_t {
    Accepted,
    Collecting,
    Assembled,
    SubmitPrepared,
    SubmitUnknown,
    RetryWaiting,
    CompletedPendingCleanup,
};

inline const char* jobPhaseToString(JobPhase p) {
    switch (p) {
        case JobPhase::Accepted:                 return "Accepted";
        case JobPhase::Collecting:               return "Collecting";
        case JobPhase::Assembled:                return "Assembled";
        case JobPhase::SubmitPrepared:           return "SubmitPrepared";
        case JobPhase::SubmitUnknown:            return "SubmitUnknown";
        case JobPhase::RetryWaiting:             return "RetryWaiting";
        case JobPhase::CompletedPendingCleanup: return "CompletedPendingCleanup";
        default:                                 return "Unknown";
    }
}

// 解析 phase 字符串；失败返回 false（调用方按损坏处置）。
bool jobPhaseFromString(const std::string& s, JobPhase& out);

// ============================================================
// TriggerReason - 上报触发原因
// ============================================================
enum class TriggerReason : std::uint8_t {
    AutoStart,
    ChangeEvent,
    CloudRequest,
    Recovery,
};

inline const char* triggerReasonToString(TriggerReason r) {
    switch (r) {
        case TriggerReason::AutoStart:     return "AutoStart";
        case TriggerReason::ChangeEvent:   return "ChangeEvent";
        case TriggerReason::CloudRequest:  return "CloudRequest";
        case TriggerReason::Recovery:      return "Recovery";
        default:                           return "Unknown";
    }
}

bool triggerReasonFromString(const std::string& s, TriggerReason& out);

// ============================================================
// SequenceState - inventory.sequence
// 最高已分配 snapshotSeq。单调递增；崩溃可产生间隙，不得回退或复用。
// ============================================================
struct SequenceState {
    std::uint32_t schemaVersion = schema::SEQUENCE_VERSION;
    std::uint64_t highestAllocated = 0;   // 已分配的最高序号；0 表示尚未分配
    Timestamp    updatedAt = 0;           // 最后分配时间 (ms since epoch)
};

// ============================================================
// LastSuccessState - inventory.last_success
// 最后一次被 TBOX 明确接受的快照。恢复/去重缓存，不替代云端基线 SSOT。
// ============================================================
struct LastSuccessState {
    std::uint32_t   schemaVersion = schema::LAST_SUCCESS_VERSION;
    std::string     reportId;
    std::uint64_t   snapshotSeq = 0;
    Timestamp       completedAt = 0;
    std::string     registryVersion;
    CollectionStatus overallResult = CollectionStatus::ALL_OK;
    std::string     fingerprint;          // 算法由后续 hash CR 固化
    VehicleSoftwareSnapshot snapshot;     // 完整成功快照
};

// ============================================================
// DedupeEntry / DedupeState - inventory.dedupe
// 有界 request/report/指纹窗口，按完成时间与 TTL 淘汰。
// 条目只保留去重所需元数据，不复制无界 payload。
// ============================================================
struct DedupeEntry {
    std::string   requestId;
    std::string   reportId;
    std::uint64_t snapshotSeq = 0;
    std::string   fingerprint;
    std::string   overallResult;   // "ALL_OK" | "PARTIAL" | "FAILED"
    Timestamp     completedAt = 0;
    Timestamp     expiresAt = 0;   // 0 表示无 TTL（仅按条目数淘汰）
};

struct DedupeState {
    std::uint32_t               schemaVersion = schema::DEDUPE_VERSION;
    std::vector<DedupeEntry>    entries;
    std::uint32_t               maxEntries = 0;   // 来自 FotaConfig
    std::int64_t                ttlMs = 0;        // 来自 FotaConfig；0 表示无 TTL
};

// ============================================================
// ActiveJobState - inventory.active_job
// 在途/重试任务检查点。按 JobPhase 保守恢复，不得把未知结果标记为成功。
// ============================================================
struct ActiveJobState {
    std::uint32_t   schemaVersion = schema::ACTIVE_JOB_VERSION;
    std::string     requestId;
    std::string     reportId;
    std::uint64_t   snapshotSeq = 0;
    TriggerReason   reason = TriggerReason::AutoStart;
    JobPhase        phase = JobPhase::Accepted;
    std::uint32_t   attempt = 0;
    Timestamp       nextRetryAt = 0;
    std::string     lastErrorCode;
    std::string     idempotencyKey;
};

} // namespace store
} // namespace cgw_fota
