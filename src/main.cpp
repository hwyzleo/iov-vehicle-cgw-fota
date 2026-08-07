#include "cgw/fota/config/fota_config.hpp"
#include "cgw/fota/store/fota_state_store.hpp"
#include "cgw/fota/store/fota_state_recovery.hpp"
#include "cgw/fota/someip/fota_provider.hpp"
#include "cgw/fota/someip/diag_inventory_client.hpp"
#include "cgw/fota/someip/tbox_inventory_client.hpp"
#include "config.h"          // cgw::fw::config::Config / LoadOptions / ConfigException
#include "constants.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include "cgw/fw/someip/runtime.hpp"
#include "cgw/fw/someip/types.hpp"
#include <iostream>
#include <filesystem>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

using namespace cgw_fota;
namespace someip_fw = cgw::fw::someip;

volatile bool running = true;

void signalHandler(int signum) {
    // 信号处理仍使用 stderr，因为 Logger 可能正在关闭
    std::cerr << "Interrupt signal (" << signum << ") received.\n";
    running = false;
}

int main(int argc, char* argv[]) {
    // Register signal handler
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_STARTING,
        "CGW-FOTA Service Starting"
    );

    // ---- 1. 建立稳定 WorkingDirectory 与 configRoots (CGW-FOTA-DSN-CR-004) ----
    namespace fs = std::filesystem;
    cgw::fw::config::LoadOptions options;
    options.cwd = fs::current_path();
    // 量产默认 /etc/cgw；argv[1] 可指定开发/测试 config 根（需含 common.yaml）。
    if (argc > 1) {
        options.configRoots = {fs::path(argv[1])};
    } else {
        options.configRoots = {"/etc/cgw"};
    }

    // ---- 2/3/4. 配置阶段：load + FotaConfig::from + logConfigFrom (统一 fail-closed) ----
    std::shared_ptr<const cgw::fw::config::ConfigSnapshot> snapshot;
    FotaConfig fotaConfig;
    cgw::fw::log::LogConfig log_config;
    try {
        snapshot = cgw::fw::config::Config::load("fota", options);
        fotaConfig = FotaConfig::from(*snapshot);
        log_config = FotaConfig::logConfigFrom(*snapshot);
    } catch (const cgw::fw::config::ConfigException& e) {
        std::cerr << "FATAL: config error [" << e.code << "] " << e.what()
                  << " (path=" << e.path << ", key=" << e.key << ")" << std::endl;
        return 1;
    } catch (const FotaConfigException& e) {
        std::cerr << "FATAL: fota config validation failed: " << e.what() << std::endl;
        return 1;
    }

    // ---- 初始化 Logger（配置阶段之后、业务模块与 SOME/IP 开放之前）----
    auto log_result = FotaLogAdapter::init("cgw-fota", log_config);
    if (log_result.error != cgw::fw::log::LogError::kOk) {
        std::cerr << "FATAL: Logger init failed: " << log_result.error_message << std::endl;
        return 1;
    }

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_CONFIG_LOADED,
        "Configuration loaded successfully",
        {flog::f_str("service", "fota")}
    );

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_LOG_INITIALIZED,
        "Logger initialized successfully",
        {flog::f_str("service", "cgw-fota"),
         flog::f_str("log_level", cgw::fw::log::logLevelToString(log_config.level)),
         flog::f_bool("strict_mode", log_config.strict)}
    );

    // ---- 5. 打开 Store、格式迁移与状态恢复 (CGW-FOTA-DSN-CR-005) ----
    std::shared_ptr<cgw_fota::store::FotaStateStore> state_store;
    cgw_fota::store::RecoveryPlan recovery_plan;
    try {
        auto store_obj = cgw_fota::store::FotaStateStore::open(
            fotaConfig.storeRoot, fotaConfig.dedupeMaxEntries, fotaConfig.dedupeTtlMs);
        state_store = std::make_shared<cgw_fota::store::FotaStateStore>(std::move(store_obj));
        recovery_plan = cgw_fota::store::StateRecovery::recover(*state_store);
    } catch (const std::exception& e) {
        std::cerr << "FATAL: store open/recovery failed: " << e.what() << std::endl;
        return 1;
    }

    if (recovery_plan.action == cgw_fota::store::RecoveryAction::Blocked) {
        FotaLogAdapter::orchestrator().error(
            cgw_fota::fota_events::STORE_RECOVERY_BLOCKED,
            "State recovery blocked, auto/active reporting disabled",
            {flog::f_str("reason", recovery_plan.reason)});
    }

    // ============================================================
    // CGW-FOTA-DSN-CR-007: 单 cgw-framework-someip Runtime 接线
    // 启动顺序：Config -> Logger -> Store -> 构造 SomeIpConfig/Runtime ->
    //   Provider/Client -> 注册 handler/availability -> runtime.start() ->
    //   request service -> 等待可用 -> offer -> ready/恢复/首次采集
    // ============================================================

    // ---- 2. 构造 SomeIpConfig，创建进程唯一 Runtime、Provider 和两个 Client ----
    someip_fw::SomeIpConfig someIpCfg = FotaConfig::someIpConfigFrom(*snapshot);

    someip_fw::SomeIpRuntime runtime;
    try {
        runtime = someip_fw::SomeIpRuntime::create(someIpCfg);
    } catch (const someip_fw::SomeIpException& e) {
        // Runtime／配置／Registry 错误阻止服务开放 (0301/0302)
        std::cerr << "FATAL: SomeIpRuntime create failed [" << e.code << "] "
                  << e.what() << std::endl;
        return 1;
    }

    // ServiceKey 与 InterfaceVersion（寻址 SSOT = constants.h 过渡 / Registry）
    someip_fw::ServiceKey fotaKey{FOTA_PROVIDER_SERVICE_ID, FOTA_PROVIDER_INSTANCE_ID};
    someip_fw::ServiceKey diagKey{DEFAULT_DIAG_SERVICE_ID, DEFAULT_DIAG_INSTANCE_ID};
    someip_fw::ServiceKey tboxKey{DEFAULT_TBOX_SERVICE_ID, DEFAULT_TBOX_INSTANCE_ID};
    someip_fw::InterfaceVersion ifaceV{1, 0};

    someip_fw::Provider fwProvider = runtime.createProvider(fotaKey, ifaceV);
    someip_fw::Client fwDiagClient = runtime.createClient(diagKey, ifaceV);
    someip_fw::Client fwTboxClient = runtime.createClient(tboxKey, ifaceV);

    // ---- 3. 构造业务适配器（包装 framework Provider/Client）----
    auto diag_client = std::make_shared<someip::DiagInventoryClient>(
        std::move(fwDiagClient), fotaConfig.diagCollectTimeout);
    auto tbox_client = std::make_shared<someip::TboxInventoryClient>(
        std::move(fwTboxClient), fotaConfig.tboxSubmitTimeout);

    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(static_cast<uint32_t>(fotaConfig.minReportInterval.count()));
    assembler->setMaxEcuCount(DEFAULT_MAX_ECU_COUNT);
    assembler->setStateStore(state_store);  // CGW-FOTA-DSN-CR-005: durable 序号分配
    assembler->setDiagRetryPolicy(fotaConfig.diagRetry.maxAttempts,
                                  static_cast<uint32_t>(fotaConfig.diagRetry.backoff.count()));

    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(fotaConfig.tboxRetry.maxAttempts,
                             static_cast<uint32_t>(fotaConfig.tboxRetry.backoff.count()));
    reporter->setDedupWindowSize(DEFAULT_DEDUP_WINDOW_SIZE);
    reporter->setStateStore(state_store);  // CGW-FOTA-DSN-CR-005
    reporter->applyRecoveryPlan(recovery_plan);  // 恢复在途任务

    auto provider = std::make_shared<someip::FotaProviderAdapter>(
        std::move(fwProvider), reporter, fotaConfig.providerAcceptBudget);

    // ---- 3. Provider 注册 Method handler；Client 注册 availability 回调 ----
    provider->registerInventoryHandler();

    // DIAG/TBOX availability 回调（仅记录状态，不直接执行采集/提交）
    diag_client->onAvailability([](someip_fw::Availability a) {
        const char* label = "Unknown";
        switch (a) {
            case someip_fw::Availability::Available:       label = "Available"; break;
            case someip_fw::Availability::Unavailable:     label = "Unavailable"; break;
            case someip_fw::Availability::VersionMismatch: label = "VersionMismatch"; break;
            case someip_fw::Availability::Stopping:        label = "Stopping"; break;
            default: break;
        }
        FotaLogAdapter::diag_client().info(
            "fota.diag.availability",
            "DIAG service availability changed",
            {flog::f_str("availability", label)});
    });
    tbox_client->onAvailability([](someip_fw::Availability a) {
        const char* label = "Unknown";
        switch (a) {
            case someip_fw::Availability::Available:       label = "Available"; break;
            case someip_fw::Availability::Unavailable:     label = "Unavailable"; break;
            case someip_fw::Availability::VersionMismatch: label = "VersionMismatch"; break;
            case someip_fw::Availability::Stopping:        label = "Stopping"; break;
            default: break;
        }
        FotaLogAdapter::inventory_reporter().info(
            "fota.tbox.availability",
            "TBOX service availability changed",
            {flog::f_str("availability", label)});
    });

    // ---- 4. runtime.start()，request DIAG/TBOX service ----
    try {
        runtime.start();
    } catch (const someip_fw::SomeIpException& e) {
        std::cerr << "FATAL: SomeIpRuntime start failed [" << e.code << "] "
                  << e.what() << std::endl;
        return 1;
    }
    diag_client->requestService();
    tbox_client->requestService();

    // ---- 5. 等待必要 Client 达到可用状态或进入有界降级；完成 Provider offer() ----
    // 有界等待 DIAG/TBOX 可用（最多 5s），未就绪则降级运行（受理后按 availability 决策）。
    {
        constexpr int kWaitMs = 5000;
        for (int i = 0; i < kWaitMs && running; ++i) {
            bool diagOk = (diag_client->availability() == someip_fw::Availability::Available);
            bool tboxOk = (tbox_client->availability() == someip_fw::Availability::Available);
            if (diagOk && tboxOk) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // ---- 5. 完成 Provider offer() ----
    provider->offer();

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_READY,
        "CGW-FOTA Service started successfully",
        {flog::f_str("someip_application", someIpCfg.application),
         flog::f_str("fota_service_id", hex_id(FOTA_PROVIDER_SERVICE_ID))}
    );

    // ---- 6. 标记服务 ready，执行恢复任务，再按配置触发首次自动采集 ----
    if (recovery_plan.action == cgw_fota::store::RecoveryAction::Blocked) {
        FotaLogAdapter::orchestrator().info("fota.service.initial_report_skipped",
            "Initial auto report skipped due to blocked recovery"
        );
    } else if (fotaConfig.autoReportOnStart) {
        FotaLogAdapter::orchestrator().info("fota.service.initial_report_starting",
            "Performing initial inventory report"
        );

        {
            // Auto-trigger generates independent context (CGW-FOTA-DSN-CR-003)
            auto scope = make_context_scope(generate_trace_id(), generate_request_id());
            if (reporter->reportInventory()) {
                FotaLogAdapter::orchestrator().info("fota.service.initial_report_succeeded",
                    "Initial inventory report successful"
                );
            } else {
                FotaLogAdapter::orchestrator().error("fota.service.initial_report_failed",
                    "Initial inventory report failed"
                );
            }
        }
    } else {
        FotaLogAdapter::orchestrator().info("fota.service.initial_report_skipped",
            "Initial auto report disabled by configuration"
        );
    }

    // Main loop
    while (running) {
        sleep(1);
    }

    // ============================================================
    // CGW-FOTA-DSN-CR-007 关闭顺序：
    //   1. 服务状态 Stopping，Provider 拒绝新请求 (stopOffer)
    //   2. 停止新自动任务并取消业务 retry timer（reporter 生命周期结束）
    //   3. 保存/flush 必要 active_job 状态（Store 持有，析构时 flush）
    //   4. release DIAG/TBOX service
    //   5. stopOffer FOTA Provider（有界等待/取消 in-flight handler/call）
    //   6. runtime.stop()，停止 executor 并验证资源清零
    // ============================================================
    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_SHUTTING_DOWN,
        "Stopping CGW-FOTA Service..."
    );

    provider->stopOffer();           // 1+5: 拒绝新请求，有界等待在途 handler
    diag_client->releaseService();   // 4: release DIAG
    tbox_client->releaseService();   // 4: release TBOX
    runtime.stop();                  // 6: 停止 Runtime/executor（幂等）

    FotaLogAdapter::orchestrator().info("fota.service.stopped",
        "CGW-FOTA Service stopped"
    );

    return 0;
}
