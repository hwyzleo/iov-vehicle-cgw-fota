#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/vehicle_condition_provider.hpp
// CGW-FOTA 车辆条件采集器端口 (CGW-FOTA-DSN-CR-009 §安装许可, US-014)
// =============================================================================
// VehicleConditionProvider 生成 conditionSetVersion + conditionSnapshot +
// localReadinessDigest；Mock 可按脚本变化门禁结果。仅在 startTime <= now < endTime、
// 任务允许且全部本地门禁通过时申请开始安装。
// =============================================================================

#include "vehicle/ota/v1/execution.pb.h"

namespace cgw_fota {
namespace ota {

class VehicleConditionProvider {
public:
    virtual ~VehicleConditionProvider() = default;

    // 生成条件集合、快照和本地就绪摘要。
    virtual ::vehicle::ota::v1::VehicleConditionSnapshot evaluateConditions() = 0;

    // 是否所有门禁通过（申请安装许可前复检；进入 INSTALL_STARTED 前再次复检）。
    virtual bool allGuardsPassed() = 0;
};

} // namespace ota
} // namespace cgw_fota
