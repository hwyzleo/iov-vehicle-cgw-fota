#include "inventory_reporter.h"
#include "error_codes.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include "constants.h"
#include <thread>
#include <chrono>

namespace cgw_fota {

InventoryReporter::InventoryReporter(std::shared_ptr<SomeIpTboxClient> tbox_client,
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

    VehicleSoftwareSnapshot snapshot;
    if (!assembler_->assembleSnapshot(snapshot)) {
        // assembleSnapshot already logs fota.snapshot.assemble.failed / fota.diag.collect.failed
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time).count();
        FotaLogAdapter::inventory_reporter().error(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle ended - snapshot assembly failed",
            {flog::f_str("report_id", current_report_id()),
             flog::f_str("overall_result", "FAILED"),
             flog::f_int("duration_ms", duration_ms)}
        );
        return false;
    }

    if (isDuplicate(snapshot.snapshot_seq)) {
        FotaLogAdapter::inventory_reporter().warn(
            "fota.inventory.duplicate_skipped",
            "Duplicate snapshot sequence detected, skipping",
            {flog::f_str("report_id", current_report_id()),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq))}
        );
        return false;
    }

    // Create child scope for TBOX submission (Service 0x6101)
    auto tbox_scope = make_someip_child_scope(
        hex_id(DEFAULT_TBOX_SERVICE_ID),
        hex_id(TBOX_METHOD_REPORT_SOFTWARE_INVENTORY)
    );

    bool result;
    if (use_retry_) {
        result = tbox_client_->reportSoftwareInventoryWithRetry(snapshot, max_retries_, retry_interval_ms_);
    } else {
        result = tbox_client_->reportSoftwareInventory(snapshot);
    }

    auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start_time).count();

    if (result) {
        addToDedupWindow(snapshot.snapshot_seq);
        // tbox_client already logs fota.tbox.submit.succeeded
        FotaLogAdapter::inventory_reporter().info(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle completed successfully",
            {flog::f_str("report_id", current_report_id()),
             flog::f_int("snapshot_seq", static_cast<int64_t>(snapshot.snapshot_seq)),
             flog::f_str("overall_result", "ALL_OK"),
             flog::f_int("duration_ms", duration_ms)}
        );
    } else {
        // tbox_client already logs fota.tbox.submit.failed
        FotaLogAdapter::inventory_reporter().error(
            fota_events::INVENTORY_REPORT_COMPLETED,
            "Inventory report cycle ended - TBOX submission failed",
            {flog::f_str("report_id", current_report_id()),
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
    // report_id 存储在 session_id 中，便于全链路关联
    std::string session_id = std::to_string(new_report_id);

    // 使用 shared_ptr 捕获 this 以延长生命周期
    auto self = shared_from_this();
    std::thread([self, new_report_id, trace_id, parent_request_id, session_id]() {
        // 在异步线程中重建上下文
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

        // 清除在途标记
        self->is_collecting_.store(false);
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
