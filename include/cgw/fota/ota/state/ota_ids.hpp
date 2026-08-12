#pragma once

// =============================================================================
// include/cgw/fota/ota/state/ota_ids.hpp
// CGW-FOTA OTA 标识类型别名 (CGW-FOTA-DSN-CR-009 §状态模型)
// =============================================================================
// Task、VehicleTask、Execution 三层状态分别建模。任务快照以 taskRevision 管理；
// 包以 packageRevision 管理；安装计划、条件集合、控制指令和策略均使用独立版本。
// 同一 VehicleTask 同时最多一个活动 Execution；重试创建新 executionId/attemptNo。
// =============================================================================

#include <cstdint>
#include <string>

namespace cgw_fota {
namespace ota {
namespace ids {

using VehicleTaskId   = std::string;
using TaskRevision    = std::string;
using ExecutionId     = std::string;
using AttemptNo       = std::uint32_t;
using PackageId       = std::string;
using PackageRevision = std::string;
using ControlRevision = std::string;
using SequenceNo      = std::uint64_t;
using StageResultId   = std::string;
using LogRequestId    = std::string;
using ConsentReceiptId = std::string;
using PermitId        = std::string;
using InventoryRevision = std::string;
using PreferenceVersion = std::string;

} // namespace ids
} // namespace ota
} // namespace cgw_fota
