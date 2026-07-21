#include "inventory_reporter.h"
#include "error_codes.h"
#include <iostream>

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

bool InventoryReporter::reportInventory(const std::string& vin) {
    std::lock_guard<std::mutex> lock(mutex_);

    VehicleSoftwareSnapshot snapshot;
    if (!assembler_->assembleSnapshot(vin, snapshot)) {
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

} // namespace cgw_fota
