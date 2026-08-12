#pragma once

// =============================================================================
// include/cgw/fota/ota/state/state_proto.hpp
// CGW-FOTA 状态机与 Protobuf 枚举映射 (CGW-FOTA-DSN-CR-009)
// =============================================================================
// 内部 C++ 状态枚举与 vehicle.ota.v1 proto 枚举互转。状态机逻辑保持纯 C++，
// 仅在编排边界做映射。
// =============================================================================

#include "cgw/fota/ota/state/execution_state.hpp"
#include "cgw/fota/ota/state/vehicle_task_state.hpp"

#include "vehicle/ota/v1/enums.pb.h"

namespace cgw_fota {
namespace ota {

::vehicle::ota::v1::VehicleTaskStatus toProto(VehicleTaskState s);
VehicleTaskState fromProtoVehicleTask(::vehicle::ota::v1::VehicleTaskStatus s);

::vehicle::ota::v1::ExecutionStatus toProto(ExecutionState s);
ExecutionState fromProtoExecution(::vehicle::ota::v1::ExecutionStatus s);

} // namespace ota
} // namespace cgw_fota
