// =============================================================================
// src/config/fota_config.cpp - CGW-FOTA 配置映射实现 (CGW-FOTA-DSN-CR-004)
// =============================================================================
// 从 cgw-framework-config 的 ConfigSnapshot 读取 fota.* 并完成类型/范围/跨字段
// 校验；从同一快照读取 common.log.* / fota.log.* 构建 LogConfig。不包含 yaml-cpp
// 直接访问。
// =============================================================================

#include "cgw/fota/config/fota_config.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace cgw_fota {

namespace {

using cgw::fw::config::ConfigSnapshot;
using cgw::fw::log::LogConfig;
using cgw::fw::log::LogLevel;
using cgw::fw::log::logLevelFromString;

// 范围契约（CGW-FOTA-REQ-CR-004 / DSN-CR-004）
constexpr std::uint32_t kMaxPendingLo = 1;
constexpr std::uint32_t kMaxPendingHi = 1024;
constexpr std::uint32_t kRetryAttemptsHi = 10;

void throwRange(const char* key, std::int64_t value,
                std::int64_t lo, std::int64_t hi) {
    throw FotaConfigException(
        std::string("config range violation: ") + key + " (" +
        std::to_string(value) + ") not in [" + std::to_string(lo) + "," +
        std::to_string(hi) + "]");
}

void requireRange(const char* key, std::int64_t value,
                  std::int64_t lo, std::int64_t hi) {
    if (value < lo || value > hi) {
        throwRange(key, value, lo, hi);
    }
}

// additionalProperties:false 的运行时等价：拒绝未知子键。
void rejectUnknown(const ConfigSnapshot& snap, const char* section,
                   const std::vector<std::string>& allowed) {
    for (const auto& k : snap.keys(section)) {
        if (std::find(allowed.begin(), allowed.end(), k) == allowed.end()) {
            throw FotaConfigException(
                std::string("unknown config key: ") + section + "." + k);
        }
    }
}

} // namespace

FotaConfigException::FotaConfigException(const std::string& message)
    : std::runtime_error(message) {}

FotaConfig FotaConfig::from(const ConfigSnapshot& s) {
    FotaConfig c;

    // ---- fota 顶层仅允许 inventory / diag / tbox / log ----
    rejectUnknown(s, "fota", {"inventory", "diag", "tbox", "log"});

    // ---- inventory.* ----
    rejectUnknown(s, "fota.inventory",
                  {"auto_report_on_start", "change_detection_enabled",
                   "min_report_interval_ms", "max_pending_requests"});

    c.autoReportOnStart =
        s.getOr<bool>("fota.inventory.auto_report_on_start", true);
    c.changeDetectionEnabled =
        s.getOr<bool>("fota.inventory.change_detection_enabled", true);

    std::int64_t minInterval =
        s.getOr<std::int64_t>("fota.inventory.min_report_interval_ms", 300000);
    requireRange("fota.inventory.min_report_interval_ms", minInterval, 0,
                 INT64_MAX);
    c.minReportInterval = std::chrono::milliseconds(minInterval);

    std::int64_t maxPending =
        s.getOr<std::int64_t>("fota.inventory.max_pending_requests", 32);
    requireRange("fota.inventory.max_pending_requests", maxPending,
                 kMaxPendingLo, kMaxPendingHi);
    c.maxPendingRequests = static_cast<std::uint32_t>(maxPending);

    // ---- diag.* ----
    rejectUnknown(s, "fota.diag",
                  {"collect_timeout_ms", "retry_max_attempts", "retry_backoff_ms"});

    std::int64_t diagTimeout =
        s.getOr<std::int64_t>("fota.diag.collect_timeout_ms", 30000);
    requireRange("fota.diag.collect_timeout_ms", diagTimeout, 1, INT64_MAX);
    c.diagCollectTimeout = std::chrono::milliseconds(diagTimeout);

    std::int64_t diagAttempts =
        s.getOr<std::int64_t>("fota.diag.retry_max_attempts", 2);
    requireRange("fota.diag.retry_max_attempts", diagAttempts, 0,
                 kRetryAttemptsHi);
    c.diagRetry.maxAttempts = static_cast<std::uint32_t>(diagAttempts);

    std::int64_t diagBackoff =
        s.getOr<std::int64_t>("fota.diag.retry_backoff_ms", 1000);
    requireRange("fota.diag.retry_backoff_ms", diagBackoff, 0, INT64_MAX);
    c.diagRetry.backoff = std::chrono::milliseconds(diagBackoff);

    // ---- tbox.* ----
    rejectUnknown(s, "fota.tbox",
                  {"submit_timeout_ms", "retry_max_attempts", "retry_backoff_ms"});

    std::int64_t tboxTimeout =
        s.getOr<std::int64_t>("fota.tbox.submit_timeout_ms", 10000);
    requireRange("fota.tbox.submit_timeout_ms", tboxTimeout, 1, INT64_MAX);
    c.tboxSubmitTimeout = std::chrono::milliseconds(tboxTimeout);

    std::int64_t tboxAttempts =
        s.getOr<std::int64_t>("fota.tbox.retry_max_attempts", 3);
    requireRange("fota.tbox.retry_max_attempts", tboxAttempts, 0,
                 kRetryAttemptsHi);
    c.tboxRetry.maxAttempts = static_cast<std::uint32_t>(tboxAttempts);

    std::int64_t tboxBackoff =
        s.getOr<std::int64_t>("fota.tbox.retry_backoff_ms", 1000);
    requireRange("fota.tbox.retry_backoff_ms", tboxBackoff, 0, INT64_MAX);
    c.tboxRetry.backoff = std::chrono::milliseconds(tboxBackoff);

    return c;
}

LogConfig FotaConfig::logConfigFrom(const ConfigSnapshot& s) {
    LogConfig lc;

    // common.log.*（CGW 公共配置，FOTA 只读消费）
    lc.schema_version =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.schema_version", 1));
    lc.level = logLevelFromString(s.getOr<std::string>("common.log.level", "INFO"));
    lc.strict = s.getOr<bool>("common.log.strict", false);

    lc.async_config.enabled = s.getOr<bool>("common.log.async.enabled", true);
    lc.async_config.queue_size =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.async.queue_size", 4096));
    lc.async_config.flush_interval_ms =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.async.flush_interval_ms", 1000));

    lc.console_config.enabled = s.getOr<bool>("common.log.console.enabled", true);

    lc.file_config.enabled = s.getOr<bool>("common.log.file.enabled", false);
    lc.file_config.root = s.getOr<std::string>("common.log.file.root", "/var/log/cgw");
    lc.file_config.max_file_size_mb =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.file.max_file_size_mb", 20));
    lc.file_config.max_files =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.file.max_files", 5));
    lc.file_config.total_budget_mb =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.file.total_budget_mb", 100));

    lc.redact_config.identifiers =
        s.getOr<std::string>("common.log.redact.identifiers", "mask");
    lc.redact_config.raw_payload_max_bytes =
        static_cast<std::uint32_t>(s.getOr<std::int64_t>("common.log.redact.payload_max_bytes", 256));

    lc.format = s.getOr<std::string>("common.log.format", "standard");

    // fota.log.* 服务级覆盖
    if (s.has("fota.log.level")) {
        lc.level = logLevelFromString(s.get<std::string>("fota.log.level"));
    }
    if (s.has("fota.log.modules")) {
        for (const auto& m : s.keys("fota.log.modules")) {
            lc.module_levels[m] =
                logLevelFromString(s.get<std::string>("fota.log.modules." + m));
        }
    }

    return lc;
}

} // namespace cgw_fota
