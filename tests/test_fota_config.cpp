// =============================================================================
// tests/test_fota_config.cpp - FotaConfig 映射单元测试 (CGW-FOTA-DSN-CR-004)
// =============================================================================
// 覆盖每个键的缺省值、类型、边界、未知字段与跨字段校验；logConfigFrom 的
// common.log.* / fota.log.* 映射。通过 cgw-framework-config 的 Config::load 构造
// 不可变快照，再经 FotaConfig::from / logConfigFrom 验证。

#include "cgw/fota/config/fota_config.hpp"
#include "config.h"
#include "config_types.h"
#include "log_types.h"
#include "config_test_util.h"

#include <gtest/gtest.h>

using namespace cgw_fota;
using cgw::fw::config::Config;
using cgw::fw::config::ConfigException;
using cgw::fw::config::LoadOptions;
using cgw::fw::config::ConfigSnapshot;
using cgw::fw::log::LogConfig;
using cgw::fw::log::LogLevel;
using cgw_fota_test::TempDir;

namespace {

const char* kFullFota = R"(
fota:
  inventory:
    auto_report_on_start: false
    change_detection_enabled: false
    min_report_interval_ms: 60000
    max_pending_requests: 64
  diag:
    collect_timeout_ms: 15000
    retry_max_attempts: 1
    retry_backoff_ms: 500
  tbox:
    submit_timeout_ms: 8000
    retry_max_attempts: 5
    retry_backoff_ms: 2000
  log:
    level: WARN
    modules:
      orchestrator: WARN
      diag_client: DEBUG
)";

// 构造一个含 common.yaml + conf.d/fota.yaml 的临时根并加载快照。
std::shared_ptr<const ConfigSnapshot> loadFota(TempDir& root, const std::string& fotaYaml) {
    root.writeCommonYaml();
    root.writeFile("conf.d/fota.yaml", fotaYaml);
    return Config::load("fota", LoadOptions{{root.path}, TempDir().path});
}

} // namespace

// ---------------------------------------------------------------------------
// 缺省值：fota 仅含 log:{}，其余键缺省。
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, DefaultsWhenKeysAbsent) {
    TempDir root;
    auto snap = loadFota(root, "fota:\n  log: {}\n");
    FotaConfig c = FotaConfig::from(*snap);

    EXPECT_TRUE(c.autoReportOnStart);
    EXPECT_TRUE(c.changeDetectionEnabled);
    EXPECT_EQ(c.minReportInterval.count(), 300000);
    EXPECT_EQ(c.maxPendingRequests, 32u);
    EXPECT_EQ(c.diagCollectTimeout.count(), 30000);
    EXPECT_EQ(c.diagRetry.maxAttempts, 2u);
    EXPECT_EQ(c.diagRetry.backoff.count(), 1000);
    EXPECT_EQ(c.tboxSubmitTimeout.count(), 10000);
    EXPECT_EQ(c.tboxRetry.maxAttempts, 3u);
    EXPECT_EQ(c.tboxRetry.backoff.count(), 1000);
}

// ---------------------------------------------------------------------------
// 覆盖值正确映射到类型化结构。
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, OverridesMapped) {
    TempDir root;
    auto snap = loadFota(root, kFullFota);
    FotaConfig c = FotaConfig::from(*snap);

    EXPECT_FALSE(c.autoReportOnStart);
    EXPECT_FALSE(c.changeDetectionEnabled);
    EXPECT_EQ(c.minReportInterval.count(), 60000);
    EXPECT_EQ(c.maxPendingRequests, 64u);
    EXPECT_EQ(c.diagCollectTimeout.count(), 15000);
    EXPECT_EQ(c.diagRetry.maxAttempts, 1u);
    EXPECT_EQ(c.diagRetry.backoff.count(), 500);
    EXPECT_EQ(c.tboxSubmitTimeout.count(), 8000);
    EXPECT_EQ(c.tboxRetry.maxAttempts, 5u);
    EXPECT_EQ(c.tboxRetry.backoff.count(), 2000);
}

// ---------------------------------------------------------------------------
// 边界：max_pending_requests 1..1024
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, RangeMaxPendingRequests) {
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  inventory:\n    max_pending_requests: 1\n");
        EXPECT_NO_THROW(FotaConfig::from(*snap));
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  inventory:\n    max_pending_requests: 1024\n");
        EXPECT_NO_THROW(FotaConfig::from(*snap));
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  inventory:\n    max_pending_requests: 0\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  inventory:\n    max_pending_requests: 2000\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
}

// ---------------------------------------------------------------------------
// 边界：retry_max_attempts 0..10（diag 与 tbox）
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, RangeRetryAttempts) {
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  diag:\n    retry_max_attempts: 0\n  tbox:\n    retry_max_attempts: 10\n");
        EXPECT_NO_THROW(FotaConfig::from(*snap));
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  diag:\n    retry_max_attempts: 11\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  tbox:\n    retry_max_attempts: 11\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
}

// ---------------------------------------------------------------------------
// 边界：timeout 必须为正；backoff 可为 0；min_report_interval 非负
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, RangeTimeoutsAndBackoff) {
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  diag:\n    collect_timeout_ms: 0\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  tbox:\n    submit_timeout_ms: 0\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  diag:\n    retry_backoff_ms: 0\n  tbox:\n    retry_backoff_ms: 0\n");
        EXPECT_NO_THROW(FotaConfig::from(*snap));
    }
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  inventory:\n    min_report_interval_ms: 0\n");
        EXPECT_NO_THROW(FotaConfig::from(*snap));
    }
}

// ---------------------------------------------------------------------------
// 未知字段 fail-closed（additionalProperties:false 等价）
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, UnknownFieldsRejected) {
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  snapshot:\n    max_ecu_count: 100\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root,
            "fota:\n  inventory:\n    auto_report_on_start: true\n    bogus: 1\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  diag:\n    collect_timeout_ms: 1\n    extra: 9\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
    {
        TempDir root;
        auto snap = loadFota(root, "fota:\n  tbox:\n    submit_timeout_ms: 1\n    extra: 9\n");
        EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
    }
}

// ---------------------------------------------------------------------------
// 类型错误 fail-closed（经 ConfigSnapshot::getInt64 抛 ConfigException CGW-FW-0004）
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, TypeMismatchFailClosed) {
    // 整数字段传入非数字字符串 -> getInt64 解析失败
    TempDir root;
    auto snap = loadFota(root,
        "fota:\n  inventory:\n    max_pending_requests: \"not-a-number\"\n");
    EXPECT_THROW(FotaConfig::from(*snap), ConfigException);
}

// ---------------------------------------------------------------------------
// 秘密字段在 fota.* 中无位置：写入即按未知字段/非法内容拒绝（运行时等价）
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, SecretFieldRejected) {
    TempDir root;
    auto snap = loadFota(root, "fota:\n  token: secret\n");
    EXPECT_THROW(FotaConfig::from(*snap), FotaConfigException);
}

// ---------------------------------------------------------------------------
// logConfigFrom：common.log.* 基线 + fota.log.* 服务级覆盖与模块级别
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, LogConfigFromSnapshot) {
    TempDir root;
    auto snap = loadFota(root, kFullFota);
    LogConfig lc = FotaConfig::logConfigFrom(*snap);

    EXPECT_EQ(lc.schema_version, 1u);
    EXPECT_EQ(lc.level, LogLevel::kWarn);            // fota.log.level 覆盖 common
    EXPECT_EQ(lc.async_config.queue_size, 4096u);    // 来自 common
    EXPECT_TRUE(lc.console_config.enabled);
    EXPECT_EQ(lc.module_levels["orchestrator"], LogLevel::kWarn);
    EXPECT_EQ(lc.module_levels["diag_client"], LogLevel::kDebug);
}

// ---------------------------------------------------------------------------
// logConfigFrom：无 fota.log 覆盖时回退 common.log.*
// ---------------------------------------------------------------------------
TEST(FotaConfigTest, LogConfigFallbackToCommon) {
    TempDir root;
    auto snap = loadFota(root, "fota:\n  log: {}\n");
    LogConfig lc = FotaConfig::logConfigFrom(*snap);

    EXPECT_EQ(lc.level, LogLevel::kInfo);            // 来自 common
    EXPECT_TRUE(lc.module_levels.empty());
}
