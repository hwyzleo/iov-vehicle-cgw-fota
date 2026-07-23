#pragma once

#include "data_models.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <memory>
#include <queue>
#include <mutex>

namespace cgw_fota {

class InventoryReporter {
public:
    InventoryReporter(std::shared_ptr<SomeIpTboxClient> tbox_client,
                     std::shared_ptr<SnapshotAssembler> assembler);
    ~InventoryReporter() = default;

    bool reportInventory();

    void setRetryPolicy(uint32_t max_retries, uint32_t retry_interval_ms);
    void setDedupWindowSize(uint32_t window_size);

private:
    std::shared_ptr<SomeIpTboxClient> tbox_client_;
    std::shared_ptr<SnapshotAssembler> assembler_;

    uint32_t max_retries_;
    uint32_t retry_interval_ms_;
    uint32_t dedup_window_size_;
    bool use_retry_;

    std::queue<uint64_t> recent_seq_numbers_;
    std::mutex mutex_;

    bool isDuplicate(uint64_t seq_number);
    void addToDedupWindow(uint64_t seq_number);
};

} // namespace cgw_fota
