#pragma once

// =============================================================================
// include/cgw/fota/config/fota_config.hpp
// CGW-FOTA 类型化配置映射 (CGW-FOTA-DSN-CR-004)
// =============================================================================
// 将 cgw-framework-config 的不可变 ConfigSnapshot 映射为只读 FotaConfig。
// 业务代码不直接接触 yaml-cpp；所有类型/范围/跨字段校验在 from() 中完成，
// 任一失败抛 FotaConfigException（fail-closed，映射为 CGW-FW-00xx 配置错误）。
//
// fota.* 契约仅含 inventory / diag / tbox / log。Service/Instance/Method ID、
// 协议与端口不进入本结构体，继续来自整车 SOME/IP Service Registry。
// =============================================================================

#include "config_types.h"   // cgw::fw::config::ConfigSnapshot
#include "log_types.h"      // cgw::fw::log::LogConfig
#include "cgw/fw/someip/types.hpp"  // cgw::fw::someip::SomeIpConfig (CR-007)

#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace cgw_fota {

// ---------------------------------------------------------------------------
// FotaConfigException - 配置校验失败（fail-closed）
// ---------------------------------------------------------------------------
class FotaConfigException : public std::runtime_error {
public:
    explicit FotaConfigException(const std::string& message);
};

// ---------------------------------------------------------------------------
// RetryConfig - 采集/提交重试策略
// ---------------------------------------------------------------------------
struct RetryConfig {
    std::uint32_t maxAttempts;
    std::chrono::milliseconds backoff;
};

// ---------------------------------------------------------------------------
// FotaConfig - 不可变 fota.* 业务配置
// ---------------------------------------------------------------------------
struct FotaConfig {
    // inventory.*
    bool autoReportOnStart;
    bool changeDetectionEnabled;
    std::chrono::milliseconds minReportInterval;
    std::uint32_t maxPendingRequests;
    // dedupe 边界 (CGW-FOTA-DSN-CR-005)
    std::uint32_t dedupeMaxEntries;
    std::int64_t dedupeTtlMs;

    // diag.*
    std::chrono::milliseconds diagCollectTimeout;
    RetryConfig diagRetry;

    // tbox.*
    std::chrono::milliseconds tboxSubmitTimeout;
    RetryConfig tboxRetry;

    // someip.* (CGW-FOTA-DSN-CR-007)
    std::chrono::milliseconds providerAcceptBudget;  // fota.someip.provider_accept_budget_ms

    // store (CGW-FOTA-DSN-CR-005)：common.store.root，缺省 /var/lib/cgw
    std::string storeRoot;

    // 从不可变 ConfigSnapshot 构建已校验的 FotaConfig。
    // 抛 FotaConfigException：缺省值以外的不合法类型、越界值、未知字段或跨字段冲突。
    static FotaConfig from(const cgw::fw::config::ConfigSnapshot& snapshot);

    // 从同一快照读取 common.log.* 与 fota.log.*，构建 Logger 配置。
    // Logger 初始化与业务配置共享同一不可变视图。
    static cgw::fw::log::LogConfig
    logConfigFrom(const cgw::fw::config::ConfigSnapshot& snapshot);

    // CGW-FOTA-DSN-CR-007: 从 common.someip.* 与 fota.someip.* 构建 framework
    // SomeIpConfig。application 固定为 "cgw-fota"；registryProfile 来自 common。
    // someip 不读 YAML，host 注入不可变配置 (CR-007 §总体架构)。
    static cgw::fw::someip::SomeIpConfig
    someIpConfigFrom(const cgw::fw::config::ConfigSnapshot& snapshot);
};

} // namespace cgw_fota
