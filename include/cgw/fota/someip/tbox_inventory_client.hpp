// =============================================================================
// include/cgw/fota/someip/tbox_inventory_client.hpp
// CGW-FOTA TBOX Inventory Client 适配器 (CGW-FOTA-DSN-CR-007 §Client 设计)
// =============================================================================
// 角色与 Registry 绑定：Client 请求 TBOX-SOMEIP Service 0x6101 / Instance 0x0001 /
// TCP 56101 的最终快照提交接口。寻址 SSOT = 整车 Registry。
//
// 调用语义（CR-007 §Client 设计）：
//   - 每次提交使用 CallOptions{timeout=fota.tbox.submit_timeout_ms, retry=None}
//   - framework 不自动重试；业务重试由 orchestrator(InventoryReporter) 统一执行
//   - 重试保持相同 reportId、snapshotSeq、snapshotFingerprint、dedupeKey 和幂等标识，
//     只创建新的 framework request/session
//   - 只有明确成功 response 才进入 last_success 流程；timeout/断线/late response
//     不得推断成功
//
// 本适配器不包含重试循环（reportSoftwareInventoryWithRetry 已移除）。
// =============================================================================

#pragma once

#include "data_models.h"
#include "cgw/fw/someip/client.hpp"
#include "cgw/fw/someip/types.hpp"

#include <chrono>
#include <memory>

namespace cgw_fota {
namespace someip {

// TBOX 快照提交 Client。业务方法保持 virtual 以便单元测试 mock。
class TboxInventoryClient {
public:
    // 生产构造：包装 framework Client，注入提交超时（fota.tbox.submit_timeout_ms）。
    TboxInventoryClient(cgw::fw::someip::Client client,
                        std::chrono::milliseconds submitTimeout);
    virtual ~TboxInventoryClient();

    // ---- framework 生命周期（透传）----
    void requestService();
    void releaseService();
    cgw::fw::someip::Availability availability() const;
    cgw::fw::someip::Subscription
    onAvailability(cgw::fw::someip::AvailabilityCallback callback);

    // ---- 业务接口（virtual 供 mock）----
    // 单次提交快照。retry=None；只有明确成功才返回 true。
    // 超时/断线/late response 返回 false（结果未知，由 orchestrator 决策重试/检查点）。
    virtual bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot);

protected:
    // 测试子类构造：不持有 framework Client。
    TboxInventoryClient();

private:
    cgw::fw::someip::Client client_;
    std::chrono::milliseconds submitTimeout_;
    bool hasClient_;
};

} // namespace someip
} // namespace cgw_fota
