// =============================================================================
// src/ota/state/state_proto.cpp
// CGW-FOTA 状态机与 Protobuf 枚举映射 (CGW-FOTA-DSN-CR-009)
// =============================================================================

#include "cgw/fota/ota/state/state_proto.hpp"

namespace cgw_fota {
namespace ota {

::vehicle::ota::v1::VehicleTaskStatus toProto(VehicleTaskState s) {
    switch (s) {
        case VehicleTaskState::None:            return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_NONE;
        case VehicleTaskState::Discovered:      return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DISCOVERED;
        case VehicleTaskState::ConsentPending:  return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_CONSENT_PENDING;
        case VehicleTaskState::DownloadPending: return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DOWNLOAD_PENDING;
        case VehicleTaskState::Downloading:     return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DOWNLOADING;
        case VehicleTaskState::Ready:           return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_READY;
        case VehicleTaskState::WaitingWindow:   return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_WAITING_WINDOW;
        case VehicleTaskState::PermitPending:   return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_PERMIT_PENDING;
        case VehicleTaskState::Executing:       return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_EXECUTING;
        case VehicleTaskState::RetryPending:    return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_RETRY_PENDING;
        case VehicleTaskState::RollbackPending: return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_ROLLBACK_PENDING;
        case VehicleTaskState::Paused:          return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_PAUSED;
        case VehicleTaskState::Completed:       return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_COMPLETED;
        case VehicleTaskState::Ended:           return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_ENDED;
    }
    return ::vehicle::ota::v1::VEHICLE_TASK_STATUS_UNSPECIFIED;
}

VehicleTaskState fromProtoVehicleTask(::vehicle::ota::v1::VehicleTaskStatus s) {
    switch (s) {
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_NONE:            return VehicleTaskState::None;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DISCOVERED:      return VehicleTaskState::Discovered;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_CONSENT_PENDING: return VehicleTaskState::ConsentPending;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DOWNLOAD_PENDING:return VehicleTaskState::DownloadPending;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_DOWNLOADING:     return VehicleTaskState::Downloading;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_READY:           return VehicleTaskState::Ready;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_WAITING_WINDOW:  return VehicleTaskState::WaitingWindow;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_PERMIT_PENDING:  return VehicleTaskState::PermitPending;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_EXECUTING:       return VehicleTaskState::Executing;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_RETRY_PENDING:   return VehicleTaskState::RetryPending;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_ROLLBACK_PENDING:return VehicleTaskState::RollbackPending;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_PAUSED:          return VehicleTaskState::Paused;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_COMPLETED:       return VehicleTaskState::Completed;
        case ::vehicle::ota::v1::VEHICLE_TASK_STATUS_ENDED:           return VehicleTaskState::Ended;
        default: return VehicleTaskState::None;
    }
}

::vehicle::ota::v1::ExecutionStatus toProto(ExecutionState s) {
    switch (s) {
        case ExecutionState::PermitPersisted: return ::vehicle::ota::v1::EXECUTION_STATUS_PERMIT_PERSISTED;
        case ExecutionState::Ready:           return ::vehicle::ota::v1::EXECUTION_STATUS_READY;
        case ExecutionState::InstallStarted:  return ::vehicle::ota::v1::EXECUTION_STATUS_INSTALL_STARTED;
        case ExecutionState::Install:         return ::vehicle::ota::v1::EXECUTION_STATUS_INSTALL;
        case ExecutionState::Reboot:          return ::vehicle::ota::v1::EXECUTION_STATUS_REBOOT;
        case ExecutionState::PostCheck:       return ::vehicle::ota::v1::EXECUTION_STATUS_POST_CHECK;
        case ExecutionState::Rollback:        return ::vehicle::ota::v1::EXECUTION_STATUS_ROLLBACK;
        case ExecutionState::ResultPending:   return ::vehicle::ota::v1::EXECUTION_STATUS_RESULT_PENDING;
        case ExecutionState::Succeeded:       return ::vehicle::ota::v1::EXECUTION_STATUS_SUCCEEDED;
        case ExecutionState::Failed:          return ::vehicle::ota::v1::EXECUTION_STATUS_FAILED;
        case ExecutionState::RolledBack:      return ::vehicle::ota::v1::EXECUTION_STATUS_ROLLED_BACK;
        case ExecutionState::Canceled:        return ::vehicle::ota::v1::EXECUTION_STATUS_CANCELED;
    }
    return ::vehicle::ota::v1::EXECUTION_STATUS_UNSPECIFIED;
}

ExecutionState fromProtoExecution(::vehicle::ota::v1::ExecutionStatus s) {
    switch (s) {
        case ::vehicle::ota::v1::EXECUTION_STATUS_PERMIT_PERSISTED: return ExecutionState::PermitPersisted;
        case ::vehicle::ota::v1::EXECUTION_STATUS_READY:            return ExecutionState::Ready;
        case ::vehicle::ota::v1::EXECUTION_STATUS_INSTALL_STARTED:  return ExecutionState::InstallStarted;
        case ::vehicle::ota::v1::EXECUTION_STATUS_INSTALL:          return ExecutionState::Install;
        case ::vehicle::ota::v1::EXECUTION_STATUS_REBOOT:           return ExecutionState::Reboot;
        case ::vehicle::ota::v1::EXECUTION_STATUS_POST_CHECK:       return ExecutionState::PostCheck;
        case ::vehicle::ota::v1::EXECUTION_STATUS_ROLLBACK:         return ExecutionState::Rollback;
        case ::vehicle::ota::v1::EXECUTION_STATUS_RESULT_PENDING:   return ExecutionState::ResultPending;
        case ::vehicle::ota::v1::EXECUTION_STATUS_SUCCEEDED:        return ExecutionState::Succeeded;
        case ::vehicle::ota::v1::EXECUTION_STATUS_FAILED:           return ExecutionState::Failed;
        case ::vehicle::ota::v1::EXECUTION_STATUS_ROLLED_BACK:      return ExecutionState::RolledBack;
        case ::vehicle::ota::v1::EXECUTION_STATUS_CANCELED:         return ExecutionState::Canceled;
        default: return ExecutionState::PermitPersisted;
    }
}

} // namespace ota
} // namespace cgw_fota
