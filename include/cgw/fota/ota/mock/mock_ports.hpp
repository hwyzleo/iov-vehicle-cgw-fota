#pragma once

// =============================================================================
// include/cgw/fota/ota/mock/mock_ports.hpp
// CGW-FOTA Mock 端口 (CGW-FOTA-DSN-CR-009 §Mock, US-017 / CGW-FOTA-DSN-CR-010)
// =============================================================================
// 仅在 FOTA_ENABLE_TEST_DOUBLES 定义时可用；量产构建不得包含。Mock 与真实实现
// 共享同一端口、状态机和 payload，不分叉上层逻辑。使用确定性时间/随机源与可复现
// 脚本，不含生产秘密。fake TBOX/cloud server 已迁移到
// fake_vehicle_message_transport.hpp（实现通用 VehicleMessageTransport 端口，按
// payloadType 分发并注入故障点）。本文件保留车内端口 Mock。
// =============================================================================

#ifndef FOTA_ENABLE_TEST_DOUBLES
#error "mock_ports.hpp is only available with FOTA_ENABLE_TEST_DOUBLES (NON_PRODUCTION)"
#endif

#include "cgw/fota/ota/call_context.hpp"
#include "cgw/fota/ota/event_sink.hpp"
#include "cgw/fota/ota/mock/scenario_script.hpp"
#include "cgw/fota/ota/ports/consent_provider.hpp"
#include "cgw/fota/ota/ports/install_executor.hpp"
#include "cgw/fota/ota/ports/inventory_provider.hpp"
#include "cgw/fota/ota/ports/log_collector.hpp"
#include "cgw/fota/ota/ports/package_downloader.hpp"
#include "cgw/fota/ota/ports/vehicle_condition_provider.hpp"

#include "vehicle/ota/v1/execution.pb.h"
#include "vehicle/ota/v1/inventory.pb.h"
#include "vehicle/ota/v1/task.pb.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <vector>

namespace cgw_fota {
namespace ota {
namespace mock {

// ---------------------------------------------------------------------------
// MockInventoryProvider - 按 scenario 产生 FULL 清单
// ---------------------------------------------------------------------------
class MockInventoryProvider : public InventoryProvider {
public:
    explicit MockInventoryProvider(ScenarioScript s) : scenario_(std::move(s)) {}

    ::vehicle::ota::v1::InventoryInfo
    collectInventory(::vehicle::ota::v1::InventoryMode mode) override {
        ::vehicle::ota::v1::InventoryInfo info;
        info.set_mode(mode == ::vehicle::ota::v1::INVENTORY_MODE_DIGEST
                          ? ::vehicle::ota::v1::INVENTORY_MODE_DIGEST
                          : ::vehicle::ota::v1::INVENTORY_MODE_FULL);
        info.set_inventory_revision("inv-rev-1");
        info.set_algorithm("sha-256");
        info.set_baseline_id(scenario_.baseline);
        info.set_baseline_source("factory");
        info.set_registry_version("1.0.0");
        info.set_ota_master_version(scenario_.otaMasterVersion);
        info.set_collected_at_ms(nowMs_);
        info.set_overall_result("all_ok");
        if (info.mode() == ::vehicle::ota::v1::INVENTORY_MODE_FULL) {
            auto* e = info.add_ecu_list();
            e->set_ecu_id("VCU-001");
            e->set_part_number("P-VCU-001");
            e->set_sw_version("1.0.0");
            e->set_source("UDS_0x22");
            e->set_status("ok");
        } else {
            auto* d = info.add_ecu_digest_list();
            d->set_ecu_id("VCU-001");
            d->set_digest_hex(std::string(64, 'a'));
        }
        info.set_ecu_list_digest(std::string(64, 'a'));
        return info;
    }

    void advanceClock(std::int64_t ms) { nowMs_ = ms; }

private:
    ScenarioScript scenario_;
    std::int64_t nowMs_ = 1000;
};

// ---------------------------------------------------------------------------
// MockConsentProvider - 默认 ACCEPTED；guard_failed/consent 故障可配置
// ---------------------------------------------------------------------------
class MockConsentProvider : public ConsentProvider {
public:
    MockConsentProvider() = default;
    ConsentChoice requestConsent(const std::string& /*vehicleTaskId*/,
                                 const ::vehicle::ota::v1::ConsentTerms& terms) override {
        ConsentChoice c;
        c.termsAvailable = true;
        c.terms = terms;
        c.userChoice = rejected_ ? ::vehicle::ota::v1::CONSENT_STATUS_REJECTED
                                 : ::vehicle::ota::v1::CONSENT_STATUS_ACCEPTED;
        return c;
    }
    void setRejected(bool r) { rejected_ = r; }

private:
    bool rejected_ = false;
};

// ---------------------------------------------------------------------------
// MockPackageDownloader - 按 scenario 模拟下载/校验，支持故障点
// ---------------------------------------------------------------------------
class MockPackageDownloader : public PackageDownloader {
public:
    explicit MockPackageDownloader(ScenarioScript s) : scenario_(std::move(s)) {}

    DownloadOutcome
    downloadAndVerify(const ::vehicle::ota::v1::PackageInfo& pkg,
                      const ::vehicle::ota::v1::DownloadGrantResponse& grant,
                      std::int64_t fromOffset) override {
        DownloadOutcome out;
        out.finalOffset = pkg.size_bytes();
        out.bytesDownloaded = pkg.size_bytes() - fromOffset;

        // etag_changed: 云端要求 reset_offset 时，下载器应已清零；此处模拟成功后状态。
        // download_disconnect / hash_failed / signature_failed: 按 failuresBeforeSuccess 注入。
        int failures = failuresFor(pkg.package_id());
        if (scenario_.hasFault("download_disconnect") && failures > 0 && !grantFailedOnce_) {
            grantFailedOnce_ = true;
            out.allStagesSucceeded = false;
            out.errorCode = "OTA-PACKAGE-DOWNLOAD";
            out.errorDetail = "download_disconnect";
            addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_DOWNLOAD,
                     ::vehicle::ota::v1::STAGE_RESULT_STATUS_FAILED, "download_disconnect");
            return out;
        }
        if (scenario_.hasFault("hash_failed") && !hashFailedOnce_) {
            hashFailedOnce_ = true;
            out.allStagesSucceeded = false;
            out.errorCode = "OTA-PACKAGE-VERIFY";
            out.errorDetail = "hash_failed";
            addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_VERIFY_HASH,
                     ::vehicle::ota::v1::STAGE_RESULT_STATUS_FAILED, "hash_failed");
            return out;
        }

        // Happy path: DOWNLOAD + VERIFY_HASH + VERIFY_SIGNATURE + DECRYPT 成功。
        addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_DOWNLOAD,
                 ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED, "");
        addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_VERIFY_HASH,
                 ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED, "");
        addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_VERIFY_SIGNATURE,
                 ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED, "");
        addStage(out, pkg, ::vehicle::ota::v1::STAGE_RESULT_TYPE_DECRYPT,
                 ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED, "");
        out.allStagesSucceeded = true;
        return out;
    }

private:
    ScenarioScript scenario_;
    bool grantFailedOnce_ = false;
    bool hashFailedOnce_ = false;

    int failuresFor(const std::string& pid) const {
        for (const auto& p : scenario_.packages) if (p.packageId == pid) return p.failuresBeforeSuccess;
        return 0;
    }
    void addStage(DownloadOutcome& out, const ::vehicle::ota::v1::PackageInfo& pkg,
                  ::vehicle::ota::v1::StageResultType t,
                  ::vehicle::ota::v1::StageResultStatus s,
                  const std::string& detail) {
        auto& r = out.stageResults.emplace_back();
        r.set_package_id(pkg.package_id());
        r.set_package_revision(pkg.package_revision());
        r.set_stage_result_id("SR-" + pkg.package_id() + "-" +
                              std::to_string(static_cast<int>(t)));
        r.set_stage_result_type(t);
        r.set_stage_result_status(s);
        r.set_stage_result_digest(std::string(64, 'b'));
        r.set_error_detail(detail);
        r.set_completed_at_ms(2000);
        r.set_bytes_downloaded(pkg.size_bytes());
    }
};

// ---------------------------------------------------------------------------
// MockVehicleConditionProvider - 默认门禁通过；guard_failed 故障可配置
// ---------------------------------------------------------------------------
class MockVehicleConditionProvider : public VehicleConditionProvider {
public:
    ::vehicle::ota::v1::VehicleConditionSnapshot evaluateConditions() override {
        ::vehicle::ota::v1::VehicleConditionSnapshot s;
        s.set_condition_set_version("cond-v1");
        s.set_condition_snapshot("snapshot-1");
        s.set_local_readiness_digest(std::string(64, 'c'));
        if (guardFailed_) {
            s.add_failed_guards("battery");
        } else {
            s.add_passed_guards("battery");
            s.add_passed_guards("parking_brake");
        }
        return s;
    }
    bool allGuardsPassed() override { return !guardFailed_; }
    void setGuardFailed(bool f) { guardFailed_ = f; }

private:
    bool guardFailed_ = false;
};

// ---------------------------------------------------------------------------
// MockLogCollector - 生成确定性无敏感日志包；log_upload_failed 故障可配置
// ---------------------------------------------------------------------------
class MockLogCollector : public LogCollector {
public:
    LogPackage collect(const ::vehicle::ota::v1::LogCollectScope& /*scope*/,
                       std::int64_t /*fromMs*/, std::int64_t /*toMs*/) override {
        LogPackage p;
        if (failNext_) {
            p.generated = false;
            p.errorCode = "OTA-LOG-COLLECT";
            return p;
        }
        p.objectKey = "mock-log-" + std::to_string(++seq_);
        p.digestHex = std::string(64, 'd');
        p.sizeBytes = 256;
        p.generated = true;
        return p;
    }
    void setFailNext(bool f) { failNext_ = f; }

private:
    std::uint64_t seq_ = 0;
    bool failNext_ = false;
};

// ---------------------------------------------------------------------------
// MockInstallExecutor - 按 scenario 驱动阶段，经 EventSink 投递事件
// 支持 install_failed、rollback_required、pause_at_safe_point、restart_after_checkpoint
// ---------------------------------------------------------------------------
class MockInstallExecutor : public InstallExecutor {
public:
    explicit MockInstallExecutor(ScenarioScript s) : scenario_(std::move(s)) {}

    PrepareResult prepare(const ::vehicle::ota::v1::FrozenTaskSnapshot& task) override {
        task_ = task;
        PrepareResult r;
        r.ready = true;
        return r;
    }

    StartResult start(const ::vehicle::ota::v1::InstallPermitResponse& permit,
                      EventSink& sink) override {
        permit_ = permit;
        sink_ = &sink;
        stageIdx_ = 0;
        progressIdx_ = 0;
        // 驱动所有阶段，经 EventSink 投递事件（编排器负责 durable+发送）
        for (stageIdx_ = 0; stageIdx_ < scenario_.stages.size(); ++stageIdx_) {
            const auto& st = scenario_.stages[stageIdx_];
            for (progressIdx_ = 0; progressIdx_ < st.progress.size(); ++progressIdx_) {
                emitEvent(st, st.progress[progressIdx_],
                          ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED);
            }
            emitEvent(st, 100, st.result == "FAILED"
                                  ? ::vehicle::ota::v1::STAGE_RESULT_STATUS_FAILED
                                  : ::vehicle::ota::v1::STAGE_RESULT_STATUS_SUCCEEDED);
            progressIdx_ = 0;
        }
        StartResult r;
        r.started = true;
        return r;
    }

    // 是否所有阶段已驱动完成。
    bool done() const { return stageIdx_ >= scenario_.stages.size(); }

    ControlApplyOutcome apply(const ::vehicle::ota::v1::ControlCommand& cmd) override {
        ControlApplyOutcome o;
        if (cmd.command_type() == ::vehicle::ota::v1::CONTROL_COMMAND_TYPE_PAUSE) {
            o.status = ::vehicle::ota::v1::CONTROL_ACK_STATUS_APPLIED;
        } else if (cmd.command_type() == ::vehicle::ota::v1::CONTROL_COMMAND_TYPE_ROLLBACK) {
            o.status = ::vehicle::ota::v1::CONTROL_ACK_STATUS_APPLIED;
            rollbackRequested_ = true;
        } else if (cmd.command_type() == ::vehicle::ota::v1::CONTROL_COMMAND_TYPE_ABORT) {
            o.status = ::vehicle::ota::v1::CONTROL_ACK_STATUS_APPLIED;
            aborted_ = true;
        } else {
            o.status = ::vehicle::ota::v1::CONTROL_ACK_STATUS_DEFERRED;
        }
        o.reason = "mock apply";
        return o;
    }

    ResumeResult resume(const ::vehicle::ota::v1::InstallCheckpoint& /*ckpt*/,
                        EventSink& sink) override {
        sink_ = &sink;
        ResumeResult r;
        r.resumed = true;
        return r;
    }

    ::vehicle::ota::v1::FinalInventory readFinalInventory() override {
        ::vehicle::ota::v1::FinalInventory fi;
        auto* inv = fi.mutable_inventory();
        inv->set_mode(::vehicle::ota::v1::INVENTORY_MODE_FULL);
        inv->set_inventory_revision("inv-rev-2");
        inv->set_algorithm("sha-256");
        inv->set_baseline_id(scenario_.baseline);
        inv->set_baseline_source("lastOta");
        inv->set_registry_version("1.0.0");
        inv->set_ota_master_version(scenario_.otaMasterVersion);
        inv->set_overall_result("all_ok");
        auto* e = inv->add_ecu_list();
        e->set_ecu_id("VCU-001");
        e->set_part_number("P-VCU-001");
        e->set_sw_version("2.0.0");
        e->set_source("UDS_0x22");
        e->set_status("ok");
        return fi;
    }

    ::vehicle::ota::v1::InstallCheckpoint checkpoint() override {
        ::vehicle::ota::v1::InstallCheckpoint c;
        c.set_execution_id(permit_.execution_id());
        if (stageIdx_ < scenario_.stages.size()) {
            c.set_stage(stageProto(scenario_.stages[stageIdx_].stage));
        }
        c.set_progress_percent(progressIdx_ > 0 ? 50 : 0);
        c.set_checkpoint_at_ms(eventTs_);
        return c;
    }

    bool rollbackRequested() const { return rollbackRequested_; }
    bool aborted() const { return aborted_; }

private:
    ScenarioScript scenario_;
    ::vehicle::ota::v1::FrozenTaskSnapshot task_;
    ::vehicle::ota::v1::InstallPermitResponse permit_;
    EventSink* sink_ = nullptr;
    std::size_t stageIdx_ = 0;
    std::size_t progressIdx_ = 0;
    std::int64_t eventTs_ = 0;
    bool rollbackRequested_ = false;
    bool aborted_ = false;

    void emitEvent(const ScenarioStage& st, std::uint32_t progress,
                   ::vehicle::ota::v1::StageResultStatus result) {
        if (!sink_) return;
        ::vehicle::ota::v1::ExecutionEvent evt;
        evt.set_execution_id(permit_.execution_id());
        evt.set_stage(stageProto(st.stage));
        evt.set_progress_percent(progress);
        evt.set_result(result);
        evt.set_timestamp_ms(++eventTs_);
        evt.set_payload_summary("mock stage event");
        sink_->emit(evt);
    }

    ::vehicle::ota::v1::ExecutionStage stageProto(const std::string& s) {
        if (s == "INSTALL")    return ::vehicle::ota::v1::EXECUTION_STAGE_INSTALL;
        if (s == "REBOOT")     return ::vehicle::ota::v1::EXECUTION_STAGE_REBOOT;
        if (s == "POST_CHECK") return ::vehicle::ota::v1::EXECUTION_STAGE_POST_CHECK;
        if (s == "ROLLBACK")   return ::vehicle::ota::v1::EXECUTION_STAGE_ROLLBACK;
        return ::vehicle::ota::v1::EXECUTION_STAGE_UNSPECIFIED;
    }
};

} // namespace mock
} // namespace ota
} // namespace cgw_fota
