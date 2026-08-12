#pragma once

// =============================================================================
// include/cgw/fota/ota/ports/install_executor.hpp
// CGW-FOTA 安装执行器端口 (CGW-FOTA-DSN-CR-009 §安装许可/事件, US-014/US-015)
// =============================================================================
// InstallExecutor 执行车内安装副作用（INSTALL/REBOOT/POST_CHECK/ROLLBACK）。
// Mock 与真实实现共享同一端口、状态机和 payload。Executor 通过 EventSink 产生
// 阶段事件，不直接接触 store 或 CloudProxy。同一 VehicleTask 同时只允许一个活动
// Execution；重试创建新 executionId/attemptNo。
// =============================================================================

#include "cgw/fota/ota/event_sink.hpp"

#include "vehicle/ota/v1/control.pb.h"
#include "vehicle/ota/v1/execution.pb.h"
#include "vehicle/ota/v1/task.pb.h"

#include <string>

namespace cgw_fota {
namespace ota {

struct PrepareResult {
    bool ready = false;
    std::string reason;
};

struct StartResult {
    bool started = false;
    std::string reason;
};

struct ControlApplyOutcome {
    ::vehicle::ota::v1::ControlAckStatus status =
        ::vehicle::ota::v1::CONTROL_ACK_STATUS_REJECTED;
    std::string reason;
};

struct ResumeResult {
    bool resumed = false;
    std::string reason;
};

class InstallExecutor {
public:
    virtual ~InstallExecutor() = default;

    // 准备安装（校验包、冻结计划）。返回是否就绪。
    virtual PrepareResult
    prepare(const ::vehicle::ota::v1::FrozenTaskSnapshot& task) = 0;

    // 开始执行。permit 提供 executionId/attemptNo/permitId/permitToken/controlRevision/
    // validUntil/离线/超时策略。事件经 sink 投递。
    virtual StartResult
    start(const ::vehicle::ota::v1::InstallPermitResponse& permit, EventSink& sink) = 0;

    // 应用控制指令（PAUSE/ABORT/ROLLBACK/RESYNC/RESUME），按 applyMode 在安全点应用。
    virtual ControlApplyOutcome
    apply(const ::vehicle::ota::v1::ControlCommand& cmd) = 0;

    // 从 checkpoint 恢复执行。
    virtual ResumeResult
    resume(const ::vehicle::ota::v1::InstallCheckpoint& ckpt, EventSink& sink) = 0;

    // 读取最终版本清单（安装后）。
    virtual ::vehicle::ota::v1::FinalInventory readFinalInventory() = 0;

    // 当前检查点（不含下载偏移；下载偏移由 packageId + ETag + offset 恢复）。
    virtual ::vehicle::ota::v1::InstallCheckpoint checkpoint() = 0;
};

} // namespace ota
} // namespace cgw_fota
