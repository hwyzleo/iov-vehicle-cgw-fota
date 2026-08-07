#include "inventory_reporter.h"
#include "cgw/fota/store/fota_state_store.hpp"
#include "cgw/fota/store/fota_state_recovery.hpp"
#include "error_codes.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include "constants.h"
#include <thread>
#include <chrono>

namespace cgw_fota {

using namespace cgw_fota::store;

InventoryReporter::InventoryReporter(std::shared_ptr<someip::TboxInventoryClient> tbox_client,
                                   std::shared_ptr<SnapshotAssembler> assembler)
    : tbox_client_(tbox_client)
    , assembler_(assembler)
    , max_retries_(0)
    , retry_interval_ms_(0)
    , dedup_window_size_(100)
    , use_retry_(false)
{
}

bool InventoryReporter::reportInventory() {
    std::lock_guard<std::mutex> lock(mutex_);

    auto start_time = std::chrono::steady_clock::now();

    // ---- 确定 reportId / seq / idempotencyKey ----
    // 优先使用恢复中的在途任务或上一轮失败残留的 active job 的稳定标识。
    std::string report_id_str;
    std::string idempotency_key;
    std::uint64_t reuse_seq = 0;          // >0 表示复用已分配序号（recovery resubmit/retry）
    TriggerReason reason = TriggerReason::AutoStart;

    if (recovered_job_.has_value()) {
        report_id_str = recovered_job_->reportId;
        idempotency_key = recovered_job_->idempotencyKey;
        reuse_seq = recovered_job_->snapshotSeq;
        reason = recovered_job_->reason;
        recovered_job_.reset();
    } else if (state_store_) {
        // 检查是否有上一轮失败残留的 active job（SubmitUnknown 等）
        auto existing = state_store_->loadActiveJob();
        if (existing.has_value()) {
            report_id_str = existing->reportId;
            idempotency_key = existing->idempotencyKey;
            reuse_seq = existing->snapshotSeq;
            reason = existing->reason;
        }
    }

    // 无既有任务：复用 async 调用者设置的 reportId，或分配新 reportId（同步路径）
    if (report_id_str.empty()) {
        uint64_t existing = current_report_id_.load();
        if (existing > 0) {
            report_id_str = std::to_string(existing);
        } else {
            uint64_t new_id = ++report_seq_;
            current_report_id_.store(new_id);
            report_id_str = std::to_string(new_id);
        }
    } else {
        // 复用既有 reportId，同步内部计数器避免后续碰撞
        try {
            uint64_t rid = std::stoull(report_id_str);
            current_report_id_.store(rid);
            if (rid > report_seq_.load()) report_seq_.store(rid);
        } catch (...) { /* reportId 非数字则保持计数器不变 */ }
    }

    // Accepted 检查点（仅新任务，reuse_seq>0 表示已有检查点）
    if (state_store_ && reuse_seq == 0) {
        saveJobCheckpoint(JobPhase::Accepted, report_id_str, 0, "", reason);
    }

    // ---- Collecting ----
    if (state_store_) {
        saveJobCheckpoint(JobPhase::Collecting, report_id_str, 0, "", reason);
    }

    VehicleSoftwareSnapshot snapshot;
    if (!assembler_->assembleSnapshot(snapshot)) {
        // 采集失败：正常失败（非崩溃），清理检查点
        if (state_store_) removeActiveJobCheckpoint();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        FotaLogAdapter::inventory_reporter().error(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle ended - snapshot assembly failed",
            {flog::f_str("report_id", report_id_str),
             flog::f_str("overall_result", "FAILED"),
             flog::f_int("duration_ms", duration_ms)}
        );
        return false;
    }

    // Recovery resubmit/retry：复用已分配序号，不使用新分配的序号（允许间隙）
    if (reuse_seq > 0) {
        snapshot.snapshot_seq = reuse_seq;
    }

    // 生成幂等标识（序号分配后确定）
    if (idempotency_key.empty()) {
        idempotency_key = "fota-" + std::to_string(snapshot.snapshot_seq) +
                          "-" + report_id_str;
    }

    // ---- Assembled 检查点 ----
    if (state_store_) {
        saveJobCheckpoint(JobPhase::Assembled, report_id_str,
                          snapshot.snapshot_seq, idempotency_key, reason);
    }

    // ---- 去重 ----
    bool dup = state_store_ ? isDuplicateStore(snapshot.snapshot_seq)
                            : isDuplicate(snapshot.snapshot_seq);
    if (dup) {
        if (state_store_) removeActiveJobCheckpoint();
        FotaLogAdapter::inventory_reporter().warn(
            "fota.inventory.duplicate_skipped",
            "Duplicate snapshot sequence detected, skipping",
            {flog::f_str("report_id", report_id_str),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq))}
        );
        return false;
    }

    // ---- SubmitPrepared 检查点（调用 TBOX 前）----
    if (state_store_) {
        saveJobCheckpoint(JobPhase::SubmitPrepared, report_id_str,
                          snapshot.snapshot_seq, idempotency_key, reason);
    }

    // 创建 TBOX 提交子作用域 (Service 0x6101)
    auto tbox_scope = make_someip_child_scope(
        hex_id(DEFAULT_TBOX_SERVICE_ID),
        hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)
    );

    // CGW-FOTA-DSN-CR-007: TBOX 业务重试由 orchestrator 统一执行（framework
    // retry=None）。保持相同 reportId、snapshotSeq、dedupeKey 和幂等标识，
    // 只创建新的 framework request/session。只有明确成功才进 last_success。
    bool result = false;
    uint32_t max_attempts = use_retry_ ? (max_retries_ + 1) : 1;
    for (uint32_t attempt = 0; attempt < max_attempts; ++attempt) {
        result = tbox_client_->reportSoftwareInventory(snapshot);
        if (result) {
            break;
        }
        // 结果未知：保存 SubmitUnknown 检查点（首次失败后）
        if (state_store_ && attempt == 0) {
            saveJobCheckpoint(JobPhase::SubmitUnknown, report_id_str,
                              snapshot.snapshot_seq, idempotency_key, reason);
        }
        if (attempt + 1 < max_attempts) {
            FotaLogAdapter::inventory_reporter().warn(
                "fota.tbox.retry",
                "Retrying TBOX submission",
                {flog::f_str("report_id", report_id_str),
                 flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
                 flog::f_int("attempt", static_cast<int64_t>(attempt + 2)),
                 flog::f_int("max_attempts", static_cast<int64_t>(max_attempts)),
                 flog::f_int("retry_interval_ms", static_cast<int64_t>(retry_interval_ms_))}
            );
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_interval_ms_));
        }
    }

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    if (result) {
        // TBOX 明确接受 -> 固定提交成功顺序：
        //   1. durable 保存 last_success
        //   2. 更新 dedupe
        //   3. 标记 CompletedPendingCleanup
        //   4. 删除 active_job
        if (state_store_) {
            saveLastSuccessFromSnapshot(snapshot, report_id_str);
            addToDedupStore(snapshot, report_id_str);
            saveJobCheckpoint(JobPhase::CompletedPendingCleanup, report_id_str,
                              snapshot.snapshot_seq, idempotency_key, reason);
            removeActiveJobCheckpoint();
        } else {
            addToDedupWindow(snapshot.snapshot_seq);
        }
        FotaLogAdapter::inventory_reporter().info(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle completed successfully",
            {flog::f_str("report_id", report_id_str),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_str("overall_result", "ALL_OK"),
             flog::f_int("duration_ms", duration_ms)}
        );
    } else {
        // 提交失败/结果未知：保存 SubmitUnknown 检查点供重试/恢复
        if (state_store_) {
            saveJobCheckpoint(JobPhase::SubmitUnknown, report_id_str,
                              snapshot.snapshot_seq, idempotency_key, reason);
        }
        FotaLogAdapter::inventory_reporter().error(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle ended - TBOX submission failed",
            {flog::f_str("report_id", report_id_str),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_str("overall_result", "FAILED"),
             flog::f_int("duration_ms", duration_ms)}
        );
    }

    return result;
}

void InventoryReporter::setRetryPolicy(uint32_t max_retries, uint32_t retry_interval_ms) {
    max_retries_ = max_retries;
    retry_interval_ms_ = retry_interval_ms;
    use_retry_ = true;
}

void InventoryReporter::setDedupWindowSize(uint32_t window_size) {
    dedup_window_size_ = window_size;
}

void InventoryReporter::setStateStore(std::shared_ptr<FotaStateStore> store) {
    state_store_ = std::move(store);
}

void InventoryReporter::applyRecoveryPlan(const RecoveryPlan& plan) {
    if (plan.shouldResumeJob() && plan.job.has_value()) {
        // 恢复任务：保存为在途任务，下次 reportInventory 复用其稳定标识
        recovered_job_ = plan.job;
        // 确保检查点存在（恢复器可能在 CompletedPendingCleanup 时已清理）
        if (state_store_ && !state_store_->loadActiveJob().has_value()) {
            saveJobCheckpoint(plan.job->phase, plan.job->reportId,
                              plan.job->snapshotSeq, plan.job->idempotencyKey,
                              plan.job->reason);
        }
        is_collecting_.store(true);
    } else if (plan.action == RecoveryAction::ReCollectFresh) {
        // active_job 损坏：新 reportId+序号保守重采集
        recovered_job_.reset();
        if (state_store_) removeActiveJobCheckpoint();
    }
    // None / Blocked: 不启动恢复任务
}

bool InventoryReporter::isDuplicate(uint64_t seq_number) {
    std::queue<uint64_t> temp = recent_seq_numbers_;
    while (!temp.empty()) {
        if (temp.front() == seq_number) {
            return true;
        }
        temp.pop();
    }
    return false;
}

void InventoryReporter::addToDedupWindow(uint64_t seq_number) {
    recent_seq_numbers_.push(seq_number);
    while (recent_seq_numbers_.size() > dedup_window_size_) {
        recent_seq_numbers_.pop();
    }
}

// ===========================================================================
// Store-backed 检查点与去重 (CGW-FOTA-DSN-CR-005)
// ===========================================================================
void InventoryReporter::saveJobCheckpoint(JobPhase phase, const std::string& reportId,
                                          std::uint64_t snapshotSeq,
                                          const std::string& idempotencyKey,
                                          TriggerReason reason) {
    if (!state_store_) return;
    ActiveJobState job;
    job.requestId = current_request_id().empty() ? reportId : current_request_id();
    job.reportId = reportId;
    job.snapshotSeq = snapshotSeq;
    job.reason = reason;
    job.phase = phase;
    job.attempt = 0;
    job.idempotencyKey = idempotencyKey;
    try {
        state_store_->saveActiveJob(job);
    } catch (const std::exception&) {
        // 检查点保存失败不阻断业务流程（崩溃恢复以最佳努力为准）
    }
}

void InventoryReporter::removeActiveJobCheckpoint() {
    if (!state_store_) return;
    try {
        state_store_->removeActiveJob();
    } catch (const std::exception&) {
        // 清理失败不阻断
    }
}

bool InventoryReporter::isDuplicateStore(std::uint64_t snapshotSeq) {
    if (!state_store_) return false;
    DedupeState d = state_store_->loadDedupe();
    Timestamp now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    for (const auto& e : d.entries) {
        if (e.snapshotSeq == snapshotSeq) {
            // TTL 过期则不算重复
            if (e.expiresAt == 0 || e.expiresAt > now) {
                return true;
            }
        }
    }
    return false;
}

void InventoryReporter::addToDedupStore(const VehicleSoftwareSnapshot& snapshot,
                                        const std::string& reportId) {
    if (!state_store_) return;
    DedupeState d = state_store_->loadDedupe();
    Timestamp now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    DedupeEntry entry;
    entry.requestId = current_request_id().empty() ? reportId : current_request_id();
    entry.reportId = reportId;
    entry.snapshotSeq = snapshot.snapshot_seq;
    entry.overallResult = collectionStatusToString(snapshot.overall_result);
    entry.completedAt = now;
    entry.expiresAt = (d.ttlMs > 0) ? now + d.ttlMs : 0;
    d.entries.push_back(entry);
    // 按条目数淘汰最旧
    while (d.entries.size() > d.maxEntries && d.maxEntries > 0) {
        d.entries.erase(d.entries.begin());
    }
    try {
        state_store_->saveDedupe(d);
    } catch (const std::exception&) {
        // 去重保存失败不阻断上报成功
    }
}

void InventoryReporter::saveLastSuccessFromSnapshot(const VehicleSoftwareSnapshot& snapshot,
                                                    const std::string& reportId) {
    if (!state_store_) return;
    LastSuccessState ls;
    ls.reportId = reportId;
    ls.snapshotSeq = snapshot.snapshot_seq;
    ls.completedAt = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    ls.registryVersion = snapshot.registry_version;
    ls.overallResult = snapshot.overall_result;
    ls.snapshot = snapshot;
    // 指纹算法由后续 hash CR 固化，本 CR 留空
    try {
        state_store_->saveLastSuccess(ls);
    } catch (const std::exception&) {
        // last_success 保存失败不回滚已接受的 TBOX 提交（崩溃恢复时对账）
    }
}

AsyncReportResult InventoryReporter::reportInventoryAsync(const std::string& request_id, 
                                                         const std::string& reason) {
    // 检查是否有在途任务
    if (is_collecting_.load()) {
        // 合并到在途任务
        uint64_t existing_report_id = current_report_id_.load();
        FotaLogAdapter::orchestrator().info(
            fota_events::INVENTORY_REQUEST_MERGED,
            "Concurrent request merged to in-flight task",
            {flog::f_str("request_id", request_id),
             flog::f_int("report_id", static_cast<int64_t>(existing_report_id))}
        );
        return {true, existing_report_id};
    }

    // 分配新的 reportId
    uint64_t new_report_id = ++report_seq_;
    is_collecting_.store(true);
    current_report_id_.store(new_report_id);

    TriggerReason trigReason = (reason == "cloud_query") ? TriggerReason::CloudRequest
                                  : (reason == "manual_retry") ? TriggerReason::ChangeEvent
                                  : TriggerReason::AutoStart;

    FotaLogAdapter::orchestrator().info(
        fota_events::INVENTORY_REQUEST_ACCEPTED,
        "Inventory request accepted",
        {flog::f_str("request_id", request_id),
         flog::f_int("report_id", static_cast<int64_t>(new_report_id)),
         flog::f_str("reason", reason)}
    );

    // 异步任务边界显式复制日志上下文（CGW-FOTA-DSN-CR-003 §上下文传播）
    std::string trace_id = current_trace_id();
    std::string parent_request_id = current_request_id();
    if (trace_id.empty()) {
        trace_id = generate_trace_id();
    }
    if (parent_request_id.empty()) {
        parent_request_id = request_id;
    }
    std::string session_id = std::to_string(new_report_id);

    auto self = shared_from_this();
    std::thread([self, new_report_id, trace_id, parent_request_id, session_id, trigReason]() {
        auto scope = make_context_scope(trace_id, parent_request_id, session_id);
        try {
            bool result = self->reportInventory();
            if (result) {
                FotaLogAdapter::orchestrator().info(
                    "fota.inventory.async_completed",
                    "Async report completed successfully",
                    {flog::f_int("report_id", static_cast<int64_t>(new_report_id))}
                );
            } else {
                FotaLogAdapter::orchestrator().error(
                    "fota.inventory.async_failed",
                    "Async report failed",
                    {flog::f_int("report_id", static_cast<int64_t>(new_report_id))}
                );
            }
        } catch (const std::exception& e) {
            FotaLogAdapter::orchestrator().error(
                "fota.inventory.async_exception",
                "Async report exception",
                {flog::f_int("report_id", static_cast<int64_t>(new_report_id)),
                 flog::f_str("error", e.what())}
            );
        }
        self->is_collecting_.store(false);
        // 重置 reportId 以便后续同步调用分配新值
        self->current_report_id_.store(0);
    }).detach();

    return {true, new_report_id};
}

bool InventoryReporter::isCollecting() const {
    return is_collecting_.load();
}

uint64_t InventoryReporter::getCurrentReportId() const {
    return current_report_id_.load();
}

} // namespace cgw_fota
