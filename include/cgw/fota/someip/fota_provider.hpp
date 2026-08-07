// =============================================================================
// include/cgw/fota/someip/fota_provider.hpp
// CGW-FOTA Provider 适配器 (CGW-FOTA-DSN-CR-007 §Provider 设计)
// =============================================================================
// 角色与 Registry 绑定：Provider 0x1120 / 0x0001 / TCP 51120，对 TBOX-TSP 提供
// METHOD_REQUEST_SOFTWARE_INVENTORY(0x0001)。寻址 SSOT = 整车 Registry。
//
// 受理语义（CR-007 §Provider 设计）：
//   - handler 在 framework 注入的业务 executor 上执行；I/O 线程只完成基础校验与投递
//   - 受理路径预算由 fota.someip.provider_accept_budget_ms 控制，默认 1000
//   - 相同 requestId 命中在途/持久化去重状态时返回相同 reportId
//   - Method response 只代表受理结果，不代表最终 TBOX/MQTT 成功
//   - payload 超限/codec 失败/非法枚举/不兼容 major version 返回 CGW-FW-0306 wire error
//   - Provider 不在 framework handler 内执行 DIAG 调用、快照组装或 TBOX 提交
//
// 适配器仅包含 IDL codec、DTO 映射和 orchestrator 调用；不含协议栈 Runtime/SD/
// socket/session/timer/后端类型 (CR-007 §代码布局)。
// =============================================================================

#pragma once

#include "inventory_reporter.h"
#include "cgw/fw/someip/provider.hpp"
#include "cgw/fw/someip/types.hpp"

#include <chrono>
#include <memory>

namespace cgw_fota {
namespace someip {

// FOTA Provider 适配器。受理入站 REQUEST_SOFTWARE_INVENTORY 并投递到 orchestrator。
class FotaProviderAdapter {
public:
    // 生产构造：包装 framework Provider，注入 orchestrator 与受理预算。
    FotaProviderAdapter(cgw::fw::someip::Provider provider,
                        std::shared_ptr<InventoryReporter> reporter,
                        std::chrono::milliseconds acceptBudget);
    virtual ~FotaProviderAdapter();

    // 注册 Method handler（MUST 在 offer() 前完成）。注册
    // METHOD_REQUEST_SOFTWARE_INVENTORY。
    void registerInventoryHandler();

    // offer / stopOffer（透传 framework Provider）。
    void offer();
    void stopOffer();
    bool offering() const;

    // 直接受理入口（供测试与同步路径）。返回 {accepted, reportId}。
    virtual AsyncReportResult handleRequest(const std::string& request_id,
                                            const std::string& reason);

protected:
    // 测试子类构造：注入 reporter 但不持有 framework Provider（handleRequest 可用，
    // offer/stopOffer 为 no-op）。
    FotaProviderAdapter(std::shared_ptr<InventoryReporter> reporter,
                        std::chrono::milliseconds acceptBudget);

private:
    cgw::fw::someip::Provider provider_;
    std::shared_ptr<InventoryReporter> reporter_;
    std::chrono::milliseconds acceptBudget_;
    bool hasProvider_;
};

} // namespace someip
} // namespace cgw_fota
