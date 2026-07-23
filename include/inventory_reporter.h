#pragma once

#include "data_models.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <memory>
#include <queue>
#include <mutex>
#include <atomic>
#include <string>

namespace cgw_fota {

/**
 * 异步上报结果
 */
struct AsyncReportResult {
    bool accepted;
    uint64_t report_id;
};

class InventoryReporter : public std::enable_shared_from_this<InventoryReporter> {
public:
    InventoryReporter(std::shared_ptr<SomeIpTboxClient> tbox_client,
                     std::shared_ptr<SnapshotAssembler> assembler);
    ~InventoryReporter() = default;

    /**
     * 同步采集上报（原有方法）
     */
    bool reportInventory();

    /**
     * 异步触发采集上报，返回受理结果
     * @param request_id 请求 ID，用于日志追踪
     * @param reason 请求原因 (cloud_query, manual_retry, integration_test)
     * @return AsyncReportResult 包含是否受理和 reportId
     */
    AsyncReportResult reportInventoryAsync(const std::string& request_id, const std::string& reason);

    /**
     * 检查是否有在途采集任务
     */
    bool isCollecting() const;

    /**
     * 获取当前在途任务的 reportId
     */
    uint64_t getCurrentReportId() const;

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

    // 并发控制 (CGW-FOTA-DSN-CR-002)
    std::atomic<bool> is_collecting_{false};
    std::atomic<uint64_t> report_seq_{0};
    std::atomic<uint64_t> current_report_id_{0};

    bool isDuplicate(uint64_t seq_number);
    void addToDedupWindow(uint64_t seq_number);
};

} // namespace cgw_fota
