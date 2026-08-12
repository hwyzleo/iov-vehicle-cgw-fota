#pragma once

// =============================================================================
// include/cgw/fota/ota/ota_orchestrator.hpp
// CGW-FOTA 车云 OTA 编排器 (CGW-FOTA-DSN-CR-009 §13.1/§13.4/§13.5, US-011~016)
// =============================================================================
// OtaOrchestrator 是 Task/VehicleTask/Execution 编排唯一入口；协议适配、状态推进
// 和车内执行分离。状态推进按 durable 边界分步执行：step() 依据当前 VehicleTask
// 状态执行下一阶段动作并 durable 保存。同时实现 EventSink，executor 事件先
// durable 写入 outbox 再分配/发送严格递增 sequenceNo。
//
// 业务重试使用 durable 状态和同一幂等身份；framework 自动重试关闭。传输失败
// （OtaCloudException）由 step() 捕获并保留状态，不推进，等待下次 step 重试。
// =============================================================================

#include "cgw/fota/ota/call_context.hpp"
#include "cgw/fota/ota/event_sink.hpp"
#include "cgw/fota/ota/ota_cloud_proxy.hpp"
#include "cgw/fota/ota/ports/consent_provider.hpp"
#include "cgw/fota/ota/ports/install_executor.hpp"
#include "cgw/fota/ota/ports/inventory_provider.hpp"
#include "cgw/fota/ota/ports/log_collector.hpp"
#include "cgw/fota/ota/ports/package_downloader.hpp"
#include "cgw/fota/ota/ports/vehicle_condition_provider.hpp"
#include "cgw/fota/ota/state/execution_state.hpp"
#include "cgw/fota/ota/state/vehicle_task_state.hpp"
#include "cgw/fota/store/ota_state_store.hpp"

#include "vehicle/ota/v1/control.pb.h"

#include <chrono>
#include <cstdint>
#include <string>

namespace cgw_fota {
namespace ota {

struct OrchestratorConfig {
    std::string protocolVersion = "ota-v1";
    std::string deviceId = "dev-mock";
    std::string vin = "MOCKVIN0000000001";
    std::chrono::milliseconds cloudCallTimeout{10000};
    std::uint32_t eventOutboxMax = 4096;
};

struct StepOutcome {
    VehicleTaskState vehicleTaskState = VehicleTaskState::None;
    ExecutionState executionState = ExecutionState::PermitPersisted;
    bool terminal = false;
    std::string action;     // 本次动作描述（checkTask/download/...）
    std::string error;      // 非空表示本次失败（含 transport cause）
    bool transportFailed = false;
};

// 控制应用结果（来自 InstallExecutor.apply）。
struct ControlOutcome {
    ::vehicle::ota::v1::ControlAckStatus status =
        ::vehicle::ota::v1::CONTROL_ACK_STATUS_REJECTED;
    std::string reason;
};

class OtaOrchestrator : public EventSink {
public:
    OtaOrchestrator(OtaCloudProxy& cloud, InventoryProvider& inv,
                    ConsentProvider& consent, PackageDownloader& dl,
                    VehicleConditionProvider& cond, InstallExecutor& exec,
                    LogCollector& log, store::ota::OtaStateStore& store,
                    OrchestratorConfig cfg);

    // 启动对账恢复：从 durable 状态恢复 VehicleTask/Execution/outbox 水位。
    void reconcileOnStart();

    // 单步推进：依据当前状态执行下一阶段动作并 durable 保存。
    StepOutcome step();

    // 注入控制指令（云端推送）。durable 保存后交给 executor。
    ControlOutcome applyControl(const ::vehicle::ota::v1::ControlCommand& cmd);

    // 策略同步（US-016）。
    bool syncPolicy();

    // ---- EventSink ----
    // 先 durable 写 outbox，再分配严格递增 sequenceNo。Executor 不指定序号。
    bool emit(const ::vehicle::ota::v1::ExecutionEvent& evt) override;

    // ---- 状态查询 ----
    VehicleTaskState vehicleTaskState() const { return vtState_; }
    ExecutionState executionState() const { return exState_; }
    std::uint64_t acceptedSequenceNo() const { return acceptedSeq_; }
    std::uint64_t nextSequenceNo() const { return nextSeq_; }

private:
    OtaCloudProxy& cloud_;
    InventoryProvider& inv_;
    ConsentProvider& consent_;
    PackageDownloader& dl_;
    VehicleConditionProvider& cond_;
    InstallExecutor& exec_;
    LogCollector& log_;
    store::ota::OtaStateStore& store_;
    OrchestratorConfig cfg_;

    VehicleTaskState vtState_ = VehicleTaskState::None;
    ExecutionState exState_ = ExecutionState::PermitPersisted;
    std::uint64_t nextSeq_ = 1;
    std::uint64_t acceptedSeq_ = 0;

    // 内存缓存（恢复时从 store 加载）
    ::vehicle::ota::v1::FrozenTaskSnapshot frozenTask_;
    ::vehicle::ota::v1::InstallPermitResponse permit_;
    std::string executionId_;
    std::uint32_t attemptNo_ = 0;

    CallContext makeCtx(const std::string& idempotencyKey) const;
    ::vehicle::common::v1::RequestEnvelope makeEnvelope(const std::string& idKey) const;
    std::int64_t nowMs() const;

    // 阶段动作
    StepOutcome doCheckTask();
    StepOutcome doConsent();
    StepOutcome doDownload();
    StepOutcome doPermit();
    StepOutcome doExecute();
    StepOutcome doFinalize();
    StepOutcome doLogUpload();

    // 持久化辅助
    void persistVehicleTask(const char* action);
    void persistExecution();
    void loadFromStore();
};

} // namespace ota
} // namespace cgw_fota
