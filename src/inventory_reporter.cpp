#include "inventory_reporter.h"
#include "error_codes.h"
#include <iostream>
#include <thread>

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

    VehicleSoftwareSnapshot snapshot;
    if (!assembler_->assembleSnapshot(snapshot)) {
        std::cout << "Failed to assemble snapshot" << std::endl;
        return false;
    }

    if (isDuplicate(snapshot.snapshot_seq)) {
        std::cout << "Duplicate snapshot sequence: " << snapshot.snapshot_seq << std::endl;
        return false;
    }

    bool result;
    if (use_retry_) {
        result = tbox_client_->reportSoftwareInventoryWithRetry(snapshot, max_retries_, retry_interval_ms_);
    } else {
        result = tbox_client_->reportSoftwareInventory(snapshot);
    }

    if (result) {
        addToDedupWindow(snapshot.snapshot_seq);
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
        std::cout << "Merging request " << request_id 
                  << " to in-flight task, reportId=" << existing_report_id << std::endl;
        return {true, existing_report_id};
    }

    // 分配新的 reportId
    uint64_t new_report_id = ++report_seq_;
    is_collecting_.store(true);
    current_report_id_.store(new_report_id);

    std::cout << "Accepted request " << request_id 
              << ", reason=" << reason 
              << ", reportId=" << new_report_id << std::endl;

    // 异步启动采集上报（在独立线程中执行）
    // 使用 shared_ptr 捕获 this 以延长生命周期
    auto self = shared_from_this();
    std::thread([self, new_report_id]() {
        try {
            // 执行同步的采集上报
            bool result = self->reportInventory();
            
            if (result) {
                std::cout << "Async report completed, reportId=" << new_report_id << std::endl;
            } else {
                std::cerr << "Async report failed, reportId=" << new_report_id << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Async report exception: " << e.what() 
                      << ", reportId=" << new_report_id << std::endl;
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
