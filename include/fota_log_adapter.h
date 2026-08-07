#pragma once

#include "log.h"
#include "log_types.h"
#include <string>
#include <cstdint>

namespace cgw_fota {

// ============================================================
// FOTA 事件名常量（CGW-FOTA-DSN-CR-003 事件目录）
// ============================================================
namespace fota_events {
// Inventory request
constexpr const char* INVENTORY_REQUEST_ACCEPTED = "fota.inventory.request.accepted";
constexpr const char* INVENTORY_REQUEST_MERGED   = "fota.inventory.request.merged";
// DIAG collect
constexpr const char* DIAG_COLLECT_SUCCEEDED     = "fota.diag.collect.succeeded";
constexpr const char* DIAG_COLLECT_FAILED        = "fota.diag.collect.failed";
// Snapshot assemble
constexpr const char* SNAPSHOT_ASSEMBLED         = "fota.snapshot.assembled";
constexpr const char* SNAPSHOT_ASSEMBLE_FAILED   = "fota.snapshot.assemble.failed";
// TBOX submit
constexpr const char* TBOX_SUBMIT_SUCCEEDED      = "fota.tbox.submit.succeeded";
constexpr const char* TBOX_SUBMIT_FAILED         = "fota.tbox.submit.failed";
// Report completed
constexpr const char* INVENTORY_REPORT_COMPLETED = "fota.inventory.report.completed";
// Service lifecycle
constexpr const char* SERVICE_STARTING           = "fota.service.starting";
constexpr const char* SERVICE_CONFIG_LOADED      = "fota.service.config_loaded";
constexpr const char* SERVICE_LOG_INITIALIZED    = "fota.service.log_initialized";
constexpr const char* SERVICE_READY              = "fota.service.ready";
constexpr const char* SERVICE_SHUTTING_DOWN      = "fota.service.shutting_down";
// Store / persistence (CGW-FOTA-DSN-CR-005)
// 日志只记 key/phase/format_version/attempt/error_code；禁止 payload/VIN/device_sn。
constexpr const char* STORE_OPEN_FAILED          = "fota.store.open.failed";
constexpr const char* STORE_SEQUENCE_FAILED      = "fota.store.sequence.failed";
constexpr const char* STORE_LOCK_FAILED          = "fota.store.lock.failed";
constexpr const char* STORE_MIGRATION_FAILED     = "fota.store.migration.failed";
constexpr const char* STORE_RECOVERY_STARTED     = "fota.store.recovery.started";
constexpr const char* STORE_RECOVERY_COMPLETED   = "fota.store.recovery.completed";
constexpr const char* STORE_RECOVERY_BLOCKED     = "fota.store.recovery.blocked";
constexpr const char* STORE_SEQ_ALLOCATED        = "fota.store.seq.allocated";
constexpr const char* STORE_SEQ_BLOCKED          = "fota.store.seq.blocked";
} // namespace fota_events

// ============================================================
// 便捷字段构造器 - 减少业务代码中的冗长写法
// ============================================================
namespace flog {
inline cgw::fw::log::Field f_str(const std::string& key, const std::string& val,
                                  cgw::fw::log::Sensitivity s = cgw::fw::log::Sensitivity::Normal) {
    return cgw::fw::log::Field(key, cgw::fw::log::FieldValue::makeString(val), s);
}
inline cgw::fw::log::Field f_int(const std::string& key, int64_t val) {
    return cgw::fw::log::Field(key, cgw::fw::log::FieldValue::makeInt(val));
}
inline cgw::fw::log::Field f_bool(const std::string& key, bool val) {
    return cgw::fw::log::Field(key, cgw::fw::log::FieldValue::makeBool(val));
}
inline cgw::fw::log::Field f_double(const std::string& key, double val) {
    return cgw::fw::log::Field(key, cgw::fw::log::FieldValue::makeDouble(val));
}
} // namespace flog

// ============================================================
// FotaLogAdapter - 封装 cgw-framework-log 的初始化和模块 Logger
//
// 模块划分（CGW-FOTA-DSN-CR-003）：
// - orchestrator:        自动/主动触发、并发合并、节流与去重
// - snapshot_assembler:  快照组装、序号和整体结果
// - diag_client:         调用 CGW-DIAG、超时和错误映射
// - inventory_reporter:  调用 TBOX-SOMEIP、重试和最终提交结果
// ============================================================
class FotaLogAdapter {
public:
    /// 初始化日志系统（在 main.cpp 中调用一次）
    static cgw::fw::log::InitResult init(
        const std::string& service,
        const cgw::fw::log::LogConfig& config
    );

    /// 获取各模块的 Logger 实例
    static cgw::fw::log::Logger orchestrator();
    static cgw::fw::log::Logger snapshot_assembler();
    static cgw::fw::log::Logger diag_client();
    static cgw::fw::log::Logger inventory_reporter();
    static cgw::fw::log::Logger store();

    /// 是否已初始化
    static bool isInitialized();

private:
    static bool s_initialized;
};

} // namespace cgw_fota
