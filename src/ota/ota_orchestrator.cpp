// =============================================================================
// src/ota/ota_orchestrator.cpp
// CGW-FOTA 车云 OTA 编排器实现 (CGW-FOTA-DSN-CR-009 §13.4/§13.5)
// =============================================================================

#include "cgw/fota/ota/ota_orchestrator.hpp"
#include "cgw/fota/ota/state/state_proto.hpp"
#include "cgw/fota/store/ota_state_serializer.hpp"

#include <chrono>
#include <sstream>

namespace cgw_fota {
namespace ota {

using namespace cgw_fota::store::ota;

namespace {
std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
} // namespace

OtaOrchestrator::OtaOrchestrator(OtaCloudProxy& cloud, InventoryProvider& inv,
                                 ConsentProvider& consent, PackageDownloader& dl,
                                 VehicleConditionProvider& cond, InstallExecutor& exec,
                                 LogCollector& log, store::ota::OtaStateStore& store,
                                 OrchestratorConfig cfg)
    : cloud_(cloud), inv_(inv), consent_(consent), dl_(dl), cond_(cond),
      exec_(exec), log_(log), store_(store), cfg_(std::move(cfg)) {}

CallContext OtaOrchestrator::makeCtx(const std::string& idempotencyKey) const {
    CallContext ctx;
    ctx.traceId = "trace-" + idempotencyKey;
    ctx.requestId = "req-" + idempotencyKey;
    ctx.idempotencyKey = idempotencyKey;
    ctx.timeout = cfg_.cloudCallTimeout;
    ctx.deviceId = cfg_.deviceId;
    ctx.vin = cfg_.vin;
    return ctx;
}

::vehicle::common::v1::RequestEnvelope
OtaOrchestrator::makeEnvelope(const std::string& idKey) const {
    ::vehicle::common::v1::RequestEnvelope e;
    e.set_request_id("req-" + idKey);
    e.set_timestamp_ms(nowMs());
    e.set_protocol_version(cfg_.protocolVersion);
    e.set_device_id(cfg_.deviceId);
    e.set_vin(cfg_.vin);
    e.set_vehicle_task_id(frozenTask_.vehicle_task_id());
    e.set_execution_id(executionId_);
    e.set_idempotency_key(idKey);
    e.set_trace_id("trace-" + idKey);
    return e;
}

std::int64_t OtaOrchestrator::nowMs() const { return ::cgw_fota::ota::nowMs(); }

// ---------------------------------------------------------------------------
// 持久化辅助
// ---------------------------------------------------------------------------
void OtaOrchestrator::persistVehicleTask(const char* action) {
    OtaVehicleTaskRecord r;
    r.vehicleTaskId = frozenTask_.vehicle_task_id();
    r.taskRevision = frozenTask_.task_revision();
    r.targetBaselineId = frozenTask_.target_baseline_id();
    r.vehicleTaskState = vehicleTaskStateToString(vtState_);
    r.frozenAtMs = frozenTask_.frozen_at_ms();
    std::string bin;
    frozenTask_.SerializeToString(&bin);
    r.frozenSnapshotPbHex = protoBinaryToHex(bin);
    r.localDispositionResult = action;
    store_.saveVehicleTask(r);
}

void OtaOrchestrator::persistExecution() {
    OtaExecutionRecord r;
    r.vehicleTaskId = frozenTask_.vehicle_task_id();
    r.executionId = executionId_;
    r.attemptNo = attemptNo_;
    r.permitId = permit_.permit_id();
    r.permitToken = permit_.permit_token();
    r.controlRevision = permit_.control_revision();
    r.validUntilMs = permit_.valid_until_ms();
    r.executionState = executionStateToString(exState_);
    r.acceptedSequenceNo = acceptedSeq_;
    r.nextSequenceNo = nextSeq_;
    std::string polBin;
    permit_.offline_policy().SerializeToString(&polBin);
    r.offlinePolicyPbHex = protoBinaryToHex(polBin);
    store_.saveExecution(r);

    OtaEventOutboxRecord ob;
    if (auto loaded = store_.loadEventOutbox()) ob = std::move(*loaded);
    ob.nextSequenceNo = nextSeq_;
    ob.acceptedSequenceNo = acceptedSeq_;
    store_.saveEventOutbox(ob);
}

void OtaOrchestrator::loadFromStore() {
    if (auto vt = store_.loadVehicleTask()) {
        vtState_ = VehicleTaskState::None;
        vehicleTaskStateFromString(vt->vehicleTaskState.c_str(), vtState_);
        std::string bin = hexToProtoBinary(vt->frozenSnapshotPbHex);
        frozenTask_.ParseFromString(bin);
    }
    if (auto ex = store_.loadExecution()) {
        executionStateFromString(ex->executionState.c_str(), exState_);
        executionId_ = ex->executionId;
        attemptNo_ = ex->attemptNo;
        acceptedSeq_ = ex->acceptedSequenceNo;
        nextSeq_ = ex->nextSequenceNo;
        std::string bin = hexToProtoBinary(ex->offlinePolicyPbHex);
        permit_.mutable_offline_policy()->ParseFromString(bin);
        permit_.set_execution_id(ex->executionId);
        permit_.set_permit_id(ex->permitId);
        permit_.set_permit_token(ex->permitToken);
        permit_.set_control_revision(ex->controlRevision);
        permit_.set_valid_until_ms(ex->validUntilMs);
    }
    if (auto ob = store_.loadEventOutbox()) {
        nextSeq_ = ob->nextSequenceNo;
        acceptedSeq_ = ob->acceptedSequenceNo;
    }
}

void OtaOrchestrator::reconcileOnStart() { loadFromStore(); }

// ---------------------------------------------------------------------------
// EventSink: 先 durable 写 outbox，再分配 sequenceNo 并发送
// ---------------------------------------------------------------------------
bool OtaOrchestrator::emit(const ::vehicle::ota::v1::ExecutionEvent& evt) {
    OtaEventOutboxRecord ob;
    if (auto loaded = store_.loadEventOutbox()) ob = std::move(*loaded);
    if (ob.entries.size() >= cfg_.eventOutboxMax) return false;

    OtaEventOutboxEntry e;
    e.sequenceNo = ob.nextSequenceNo;
    e.eventId = evt.event_id().empty() ? ("EVT-" + std::to_string(e.sequenceNo))
                                       : evt.event_id();
    e.eventDigest = evt.event_digest().empty() ? std::string(64, 'f')
                                                : evt.event_digest();
    e.stage = ::vehicle::ota::v1::ExecutionStage_Name(evt.stage());
    e.progressPercent = evt.progress_percent();
    e.result = ::vehicle::ota::v1::StageResultStatus_Name(evt.result());
    e.timestampMs = evt.timestamp_ms();
    e.payloadSummary = evt.payload_summary();
    e.sendStatus = "PENDING";
    std::string bin;
    evt.SerializeToString(&bin);
    e.eventPbHex = protoBinaryToHex(bin);

    ob.entries.push_back(std::move(e));
    nextSeq_ = ob.nextSequenceNo + 1;
    ob.nextSequenceNo = nextSeq_;
    store_.saveEventOutbox(ob);
    return true;
}

// ---------------------------------------------------------------------------
// step: 状态驱动分步推进
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::step() {
    StepOutcome o;
    o.vehicleTaskState = vtState_;
    o.executionState = exState_;
    if (isTerminalVehicleTaskState(vtState_)) {
        o.terminal = true;
        o.action = "terminal";
        return o;
    }
    switch (vtState_) {
        case VehicleTaskState::None:
        case VehicleTaskState::Discovered:
            return doCheckTask();
        case VehicleTaskState::ConsentPending:
            return doConsent();
        case VehicleTaskState::DownloadPending:
        case VehicleTaskState::Downloading:
            return doDownload();
        case VehicleTaskState::Ready:
        case VehicleTaskState::WaitingWindow:
            return doPermit();
        case VehicleTaskState::PermitPending:
            return doPermit();
        case VehicleTaskState::Executing:
            return doExecute();
        case VehicleTaskState::RetryPending:
            vtState_ = VehicleTaskState::PermitPending;
            return doPermit();
        case VehicleTaskState::RollbackPending:
            exState_ = ExecutionState::Rollback;
            vtState_ = VehicleTaskState::Executing;
            return doExecute();
        default:
            o.action = "noop";
            return o;
    }
}

// ---------------------------------------------------------------------------
// 1. 任务检测
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::doCheckTask() {
    StepOutcome o;
    o.action = "checkTask";
    auto inv = inv_.collectInventory(::vehicle::ota::v1::INVENTORY_MODE_FULL);
    ::vehicle::ota::v1::TaskCheckRequest req;
    *req.mutable_envelope() = makeEnvelope("checkTask-" + frozenTask_.vehicle_task_id());
    *req.mutable_inventory() = inv;

    ::vehicle::ota::v1::TaskCheckResponse resp;
    try {
        resp = cloud_.checkTask(req, makeCtx(req.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        o.transportFailed = true;
        return o;
    }

    if (resp.inventory_disposition() != ::vehicle::ota::v1::INVENTORY_DISPOSITION_ACCEPTED) {
        // FULL_REQUIRED/REVISION_CONFLICT/... -> 记录要求，下次 FULL，停留 None
        OtaInventoryRecord ir;
        ir.mode = "FULL";
        ir.inventoryRevision = inv.inventory_revision();
        ir.algorithm = inv.algorithm();
        ir.ecuListDigest = inv.ecu_list_digest();
        ir.collectedAtMs = nowMs();
        ir.fullRequired = true;
        store_.saveInventory(ir);
        o.error = "inventory not accepted: " +
                  ::vehicle::ota::v1::InventoryDisposition_Name(resp.inventory_disposition());
        return o;
    }

    // 冻结任务快照
    frozenTask_.set_vehicle_task_id(resp.vehicle_task_id());
    frozenTask_.set_task_revision(resp.task_revision());
    frozenTask_.set_target_baseline_id(resp.target_baseline_id());
    *frozenTask_.mutable_time_window() = resp.time_window();
    *frozenTask_.mutable_policy() = resp.policy();
    frozenTask_.mutable_packages()->CopyFrom(resp.packages());
    frozenTask_.set_plan_version(resp.plan_version());
    frozenTask_.set_frozen_at_ms(nowMs());

    vtState_ = VehicleTaskState::Discovered;
    persistVehicleTask("checkTask");

    // 授权要求：MVP 假设需要授权 -> ConsentPending
    vtState_ = VehicleTaskState::ConsentPending;
    persistVehicleTask("consentRequired");
    o.vehicleTaskState = vtState_;
    return o;
}

// ---------------------------------------------------------------------------
// 2. 授权
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::doConsent() {
    StepOutcome o;
    o.action = "consent";
    ::vehicle::ota::v1::ConsentTerms terms;
    terms.set_terms_id("T-1");
    terms.set_terms_version("v1");
    terms.set_scope_digest(std::string(64, 'a'));

    auto choice = consent_.requestConsent(frozenTask_.vehicle_task_id(), terms);
    ::vehicle::ota::v1::ConsentReport report;
    *report.mutable_envelope() = makeEnvelope("consent-" + frozenTask_.vehicle_task_id());
    report.set_vehicle_task_id(frozenTask_.vehicle_task_id());
    report.set_task_revision(frozenTask_.task_revision());
    *report.mutable_terms() = terms;
    report.set_user_choice(choice.userChoice);
    report.set_reported_at_ms(nowMs());

    ::vehicle::ota::v1::ConsentResponse resp;
    try {
        resp = cloud_.reportConsent(report, makeCtx(report.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        o.transportFailed = true;
        return o;
    }

    // 仅以 effectiveConsentStatus 推进（不以 accepted 推断）
    OtaConsentRecord cr;
    cr.vehicleTaskId = frozenTask_.vehicle_task_id();
    cr.effectiveStatus = ::vehicle::ota::v1::ConsentStatus_Name(resp.effective_consent_status());
    cr.receiptId = resp.consent_receipt_id();
    cr.receiptExpiresAtMs = resp.receipt_expires_at_ms();
    cr.termsId = terms.terms_id();
    cr.termsVersion = terms.terms_version();
    ::vehicle::ota::v1::ConsentReceipt receipt;
    receipt.set_receipt_id(resp.consent_receipt_id());
    *receipt.mutable_terms() = terms;
    receipt.set_accepted_at_ms(nowMs());
    receipt.set_expires_at_ms(resp.receipt_expires_at_ms());
    receipt.set_vehicle_task_id(frozenTask_.vehicle_task_id());
    std::string rbin;
    receipt.SerializeToString(&rbin);
    cr.consentReceiptPbHex = protoBinaryToHex(rbin);
    store_.saveConsent(cr);

    if (resp.effective_consent_status() == ::vehicle::ota::v1::CONSENT_STATUS_ACCEPTED) {
        vtState_ = VehicleTaskState::DownloadPending;
    } else {
        // REJECTED/REVOKED -> Ended
        vtState_ = VehicleTaskState::Ended;
    }
    persistVehicleTask("consent");
    o.vehicleTaskState = vtState_;
    return o;
}

// ---------------------------------------------------------------------------
// 3. 下载与校验
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::doDownload() {
    StepOutcome o;
    o.action = "download";
    OtaDownloadsRecord dr;
    if (auto loaded = store_.loadDownloads()) dr = std::move(*loaded);

    bool allReady = true;
    for (const auto& pkg : frozenTask_.packages()) {
        // 查找或创建条目
        OtaDownloadEntry* entry = nullptr;
        for (auto& e : dr.entries) {
            if (e.packageId == pkg.package_id()) { entry = &e; break; }
        }
        if (!entry) {
            OtaDownloadEntry ne;
            ne.packageId = pkg.package_id();
            ne.packageRevision = pkg.package_revision();
            ne.etag = pkg.etag();
            ne.offset = 0;
            ne.verifyStatus = "PENDING";
            ne.ready = false;
            dr.entries.push_back(std::move(ne));
            entry = &dr.entries.back();
        }
        if (entry->ready) continue;

        // 申请下载凭证
        ::vehicle::ota::v1::DownloadGrantRequest greq;
        *greq.mutable_envelope() = makeEnvelope("dl-" + pkg.package_id());
        greq.set_vehicle_task_id(frozenTask_.vehicle_task_id());
        greq.set_task_revision(frozenTask_.task_revision());
        greq.set_package_id(pkg.package_id());
        greq.set_package_revision(entry->packageRevision);
        greq.set_current_offset(entry->offset);

        ::vehicle::ota::v1::DownloadGrantResponse grant;
        try {
            grant = cloud_.requestDownload(greq, makeCtx(greq.envelope().idempotency_key()));
        } catch (const OtaCloudException& e) {
            o.error = e.what();
            o.transportFailed = true;
            vtState_ = VehicleTaskState::Downloading;
            store_.saveDownloads(dr);
            return o;
        }

        // RESET_OFFSET: 清零偏移并替换 etag/packageRevision
        if (grant.reset_offset()) {
            entry->offset = 0;
            entry->etag = grant.etag();
            entry->packageRevision = grant.package_revision();
        }

        auto outcome = dl_.downloadAndVerify(pkg, grant, entry->offset);
        entry->offset = outcome.finalOffset;

        // 上报各阶段结果（独立结果接口，不用安装事件推进准备状态）
        for (const auto& sr : outcome.stageResults) {
            ::vehicle::ota::v1::StageResultReport srep = sr;
            *srep.mutable_envelope() = makeEnvelope("sr-" + pkg.package_id() +
                                                    "-" + sr.stage_result_id());
            srep.set_vehicle_task_id(frozenTask_.vehicle_task_id());
            srep.set_task_revision(frozenTask_.task_revision());
            try {
                auto sresp = cloud_.reportStageResult(
                    srep, makeCtx(srep.envelope().idempotency_key()));
                if (!sresp.accepted()) {
                    entry->verifyStatus = "FAILED";
                    entry->ready = false;
                    allReady = false;
                }
            } catch (const OtaCloudException& e) {
                o.error = e.what();
                o.transportFailed = true;
                vtState_ = VehicleTaskState::Downloading;
                store_.saveDownloads(dr);
                return o;
            }
        }

        if (outcome.allStagesSucceeded) {
            entry->verifyStatus = "SUCCEEDED";
            entry->ready = true;
            if (!outcome.stageResults.empty()) {
                entry->stageResultId = outcome.stageResults.back().stage_result_id();
                entry->stageResultDigest = outcome.stageResults.back().stage_result_digest();
            }
        } else {
            entry->verifyStatus = "FAILED";
            entry->ready = false;
            allReady = false;
            o.error = outcome.errorDetail.empty() ? "download/verify failed" : outcome.errorDetail;
        }
    }

    dr.allReady = allReady;
    store_.saveDownloads(dr);

    if (allReady) {
        dr.packageManifestDigestHex = std::string(64, 'm');
        dr.packageManifestAlgorithm = "sha-256";
        store_.saveDownloads(dr);
        vtState_ = VehicleTaskState::Ready;
    } else {
        vtState_ = VehicleTaskState::Downloading;
    }
    persistVehicleTask("download");
    o.vehicleTaskState = vtState_;
    return o;
}

// ---------------------------------------------------------------------------
// 4. 安装许可
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::doPermit() {
    StepOutcome o;
    if (vtState_ == VehicleTaskState::Ready) {
        vtState_ = VehicleTaskState::WaitingWindow;
        persistVehicleTask("waitingWindow");
        o.action = "waitingWindow";
        o.vehicleTaskState = vtState_;
        return o;
    }
    o.action = "permit";

    // 复检窗口与本地门禁
    const auto& tw = frozenTask_.time_window();
    std::int64_t t = nowMs();
    if (t < tw.start_time_ms() || t >= tw.end_time_ms()) {
        o.error = "install window not open";
        return o;  // 停留 WaitingWindow
    }
    if (!cond_.allGuardsPassed()) {
        o.error = "local guards not passed";
        return o;
    }

    auto condSnap = cond_.evaluateConditions();
    ::vehicle::ota::v1::InstallPermitRequest preq;
    *preq.mutable_envelope() = makeEnvelope("permit-" + frozenTask_.vehicle_task_id());
    preq.set_vehicle_task_id(frozenTask_.vehicle_task_id());
    preq.set_task_revision(frozenTask_.task_revision());
    preq.set_plan_version(frozenTask_.plan_version());
    preq.set_package_manifest_digest_hex(std::string(64, 'm'));
    preq.set_consent_receipt_id(store_.loadConsent().value_or(OtaConsentRecord{}).receiptId);
    *preq.mutable_condition() = condSnap;

    ::vehicle::ota::v1::InstallPermitResponse resp;
    try {
        resp = cloud_.requestInstall(preq, makeCtx(preq.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        o.transportFailed = true;
        return o;
    }

    if (!resp.permitted()) {
        o.error = "install denied: " + resp.deny_reason();
        return o;  // 停留 PermitPending/WaitingWindow，等待重试
    }

    // durable 保存 Execution/permit/controlRevision/validUntil/离线/超时策略后再执行
    permit_ = resp;
    executionId_ = resp.execution_id();
    attemptNo_ = resp.attempt_no();
    exState_ = ExecutionState::PermitPersisted;
    vtState_ = VehicleTaskState::Executing;
    persistVehicleTask("permit");
    persistExecution();

    // 进入 INSTALL_STARTED 前复检 validUntil 与本地门禁
    if (nowMs() >= resp.valid_until_ms() || !cond_.allGuardsPassed()) {
        o.error = "pre-install recheck failed (validUntil/guards)";
        return o;
    }

    o.vehicleTaskState = vtState_;
    o.executionState = exState_;
    return o;
}

// ---------------------------------------------------------------------------
// 5. 执行事件、控制与最终结果
// ---------------------------------------------------------------------------
StepOutcome OtaOrchestrator::doExecute() {
    StepOutcome o;
    o.action = "execute";

    // 首次：prepare + start
    if (exState_ == ExecutionState::PermitPersisted || exState_ == ExecutionState::Ready) {
        auto prep = exec_.prepare(frozenTask_);
        if (!prep.ready) {
            o.error = "executor prepare failed: " + prep.reason;
            return o;
        }
        auto st = exec_.start(permit_, *this);
        if (!st.started) {
            o.error = "executor start failed: " + st.reason;
            return o;
        }
        exState_ = ExecutionState::InstallStarted;
        persistExecution();
    }

    // pump 所有剩余事件（start() 内经 emit() 持久化到 outbox）
    // start() 已在首次进入时调用并投递全部阶段事件

    // 发送 outbox 中 PENDING 事件，推进水位；处理 BUFFERED/RESYNC 补发
    OtaEventOutboxRecord ob;
    if (auto loaded = store_.loadEventOutbox()) ob = std::move(*loaded);

    bool sendError = false;
    for (auto& e : ob.entries) {
        if (e.sendStatus == "ACKED") continue;
        ::vehicle::ota::v1::ExecutionEvent evt;
        std::string bin = hexToProtoBinary(e.eventPbHex);
        evt.ParseFromString(bin);
        evt.set_sequence_no(e.sequenceNo);
        *evt.mutable_envelope() = makeEnvelope("evt-" + std::to_string(e.sequenceNo));
        try {
            auto eresp = cloud_.reportEvent(evt, makeCtx(evt.envelope().idempotency_key()));
            if (eresp.status() == ::vehicle::ota::v1::EVENT_RESPONSE_STATUS_ACCEPTED) {
                e.sendStatus = "ACKED";
                acceptedSeq_ = std::max(acceptedSeq_, e.sequenceNo);
            } else {
                // BUFFERED/RESYNC：保持 PENDING，下次 step 补发
                sendError = true;
            }
        } catch (const OtaCloudException& ex) {
            o.error = ex.what();
            o.transportFailed = true;
            sendError = true;
        }
    }
    ob.acceptedSequenceNo = acceptedSeq_;
    store_.saveEventOutbox(ob);
    persistExecution();

    if (sendError) {
        o.vehicleTaskState = vtState_;
        o.executionState = exState_;
        return o;  // 等待下次 step 补发
    }

    // 全部事件 ACKED 且 executor 完成 -> 最终结果
    return doFinalize();
}

StepOutcome OtaOrchestrator::doFinalize() {
    StepOutcome o;
    o.action = "finalResult";

    auto finalInv = exec_.readFinalInventory();
    ::vehicle::ota::v1::FinalResult fr;
    *fr.mutable_envelope() = makeEnvelope("final-" + executionId_);
    fr.set_execution_id(executionId_);
    *fr.mutable_final_inventory() = finalInv;
    fr.set_result_status(exState_ == ExecutionState::Rollback
                             ? ::vehicle::ota::v1::EXECUTION_STATUS_ROLLED_BACK
                             : ::vehicle::ota::v1::EXECUTION_STATUS_SUCCEEDED);
    fr.set_control_revision(permit_.control_revision());
    fr.set_final_sequence_no(acceptedSeq_);
    fr.set_completed_at_ms(nowMs());

    ::vehicle::ota::v1::FinalResultResponse resp;
    try {
        resp = cloud_.reportFinalResult(fr, makeCtx(fr.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        o.transportFailed = true;
        return o;
    }

    if (!resp.result_accepted()) {
        o.error = "final result not accepted";
        return o;  // 等待补事件或对账
    }

    // 日志上传（US-016）
    doLogUpload();

    // 收口 Execution
    exState_ = (resp.vehicle_task_status() == ::vehicle::ota::v1::VEHICLE_TASK_STATUS_RETRY_PENDING)
                   ? ExecutionState::Failed
                   : (fr.result_status() == ::vehicle::ota::v1::EXECUTION_STATUS_ROLLED_BACK
                          ? ExecutionState::RolledBack
                          : ExecutionState::Succeeded);
    vtState_ = fromProtoVehicleTask(resp.vehicle_task_status());
    if (vtState_ == VehicleTaskState::None) {
        vtState_ = isTerminalExecutionState(exState_) ? VehicleTaskState::Completed
                                                      : VehicleTaskState::Completed;
    }
    persistVehicleTask("finalResult");
    persistExecution();
    o.vehicleTaskState = vtState_;
    o.executionState = exState_;
    o.terminal = isTerminalVehicleTaskState(vtState_);
    return o;
}

StepOutcome OtaOrchestrator::doLogUpload() {
    StepOutcome o;
    o.action = "logUpload";
    ::vehicle::ota::v1::LogGrantRequest lreq;
    *lreq.mutable_envelope() = makeEnvelope("log-" + frozenTask_.vehicle_task_id());
    lreq.set_vehicle_task_id(frozenTask_.vehicle_task_id());
    lreq.set_log_request_id("LOG-" + std::to_string(nowMs()));
    lreq.mutable_scope()->add_component_ids("cgw-fota");
    lreq.set_time_range_from_ms(0);
    lreq.set_time_range_to_ms(nowMs());

    ::vehicle::ota::v1::LogGrantResponse gresp;
    try {
        gresp = cloud_.requestLogUpload(lreq, makeCtx(lreq.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        return o;
    }
    if (!gresp.granted()) {
        o.error = "log grant denied";
        return o;
    }

    auto pkg = log_.collect(lreq.scope(), lreq.time_range_from_ms(), lreq.time_range_to_ms());
    if (!pkg.generated) {
        o.error = "log collect failed";
        return o;
    }
    ::vehicle::ota::v1::LogUploadResult lres;
    *lres.mutable_envelope() = makeEnvelope("logres-" + lreq.log_request_id());
    lres.set_log_request_id(lreq.log_request_id());
    lres.set_object_key(gresp.object_key());
    lres.set_digest_hex(pkg.digestHex);
    lres.set_size_bytes(pkg.sizeBytes);
    lres.set_status(::vehicle::ota::v1::LOG_UPLOAD_STATUS_UPLOADED);
    lres.set_completed_at_ms(nowMs());
    try {
        cloud_.reportLogUpload(lres, makeCtx(lres.envelope().idempotency_key()));
    } catch (const OtaCloudException& e) {
        o.error = e.what();
        return o;
    }

    OtaLogJobsRecord ljr;
    if (auto loaded = store_.loadLogJobs()) ljr = std::move(*loaded);
    OtaLogJobEntry je;
    je.logRequestId = lreq.log_request_id();
    je.objectKey = gresp.object_key();
    je.digestHex = pkg.digestHex;
    je.sizeBytes = pkg.sizeBytes;
    je.status = "UPLOADED";
    je.completedAtMs = nowMs();
    ljr.entries.push_back(std::move(je));
    store_.saveLogJobs(ljr);
    return o;
}

// ---------------------------------------------------------------------------
// 控制指令
// ---------------------------------------------------------------------------
ControlOutcome OtaOrchestrator::applyControl(const ::vehicle::ota::v1::ControlCommand& cmd) {
    ControlOutcome o;
    // durable 保存控制指令（按 controlRevision 去重）
    OtaControlsRecord cr;
    if (auto loaded = store_.loadControls()) cr = std::move(*loaded);
    for (const auto& e : cr.entries) {
        if (e.controlRevision == cmd.control_revision()) {
            o.status = ::vehicle::ota::v1::CONTROL_ACK_STATUS_SUPERSEDED;
            o.reason = "duplicate controlRevision";
            return o;
        }
    }
    OtaControlEntry ce;
    ce.controlRevision = cmd.control_revision();
    ce.commandType = ::vehicle::ota::v1::ControlCommandType_Name(cmd.command_type());
    ce.applyMode = ::vehicle::ota::v1::ControlApplyMode_Name(cmd.apply_mode());
    ce.expiresAtMs = cmd.expires_at_ms();
    ce.reason = cmd.reason();
    cr.entries.push_back(std::move(ce));
    store_.saveControls(cr);

    auto app = exec_.apply(cmd);
    o.status = app.status;
    o.reason = app.reason;
    if (app.status == ::vehicle::ota::v1::CONTROL_ACK_STATUS_APPLIED) {
        cr.entries.back().ackStatus =
            ::vehicle::ota::v1::ControlAckStatus_Name(app.status);
        cr.entries.back().appliedAtMs = nowMs();
        cr.lastAppliedRevision = cmd.control_revision();
        store_.saveControls(cr);

        // 上报控制回执
        ::vehicle::ota::v1::ControlAck ack;
        *ack.mutable_envelope() = makeEnvelope("ctrlack-" + cmd.control_revision());
        ack.set_vehicle_task_id(frozenTask_.vehicle_task_id());
        ack.set_execution_id(executionId_);
        ack.set_control_revision(cmd.control_revision());
        ack.set_ack_sequence_no(++nextSeq_);
        ack.set_status(app.status);
        ack.set_applied_at_ms(nowMs());
        ack.set_reason(app.reason);
        try {
            cloud_.acknowledgeControl(ack, makeCtx(ack.envelope().idempotency_key()));
        } catch (const OtaCloudException&) {
            // 回执失败保留 PENDING，由 outbox/对账补发（MVP 记录）
        }
    }
    return o;
}

// ---------------------------------------------------------------------------
// 策略同步
// ---------------------------------------------------------------------------
bool OtaOrchestrator::syncPolicy() {
    OtaPolicyRecord cur;
    if (auto loaded = store_.loadPolicy()) cur = std::move(*loaded);
    ::vehicle::ota::v1::PolicyRequest preq;
    *preq.mutable_envelope() = makeEnvelope("policy-sync");
    preq.set_base_preference_version(cur.basePreferenceVersion);
    ::vehicle::ota::v1::PolicyResponse resp;
    try {
        resp = cloud_.syncPolicy(preq, makeCtx(preq.envelope().idempotency_key()));
    } catch (const OtaCloudException&) {
        return false;
    }
    OtaPolicyRecord nr;
    nr.basePreferenceVersion = cur.preferenceVersion;
    nr.preferenceVersion = resp.preference_version();
    std::string bin;
    resp.effective_policy().SerializeToString(&bin);
    nr.effectivePolicyPbHex = protoBinaryToHex(bin);
    nr.conflict = resp.conflict();
    store_.savePolicy(nr);
    return !resp.conflict();
}

} // namespace ota
} // namespace cgw_fota
