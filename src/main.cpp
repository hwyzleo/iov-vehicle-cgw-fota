#include "cgw/fota/config/fota_config.hpp"
#include "cgw/fota/store/fota_state_store.hpp"
#include "cgw/fota/store/fota_state_recovery.hpp"
#include "config.h"          // cgw::fw::config::Config / LoadOptions / ConfigException
#include "constants.h"
#include "someip_fota_client.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "someip_fota_provider.h"
#include "fota_log_adapter.h"
#include "fota_log_context.h"
#include <iostream>
#include <filesystem>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

using namespace cgw_fota;

volatile bool running = true;

void signalHandler(int signum) {
    // 信号处理仍使用 stderr，因为 Logger 可能正在关闭
    std::cerr << "Interrupt signal (" << signum << ") received.\n";
    running = false;
}

/**
 * @brief 连接服务（带重试机制）
 * @param client SOME/IP 客户端
 * @param service_name 服务名称（用于日志）
 * @param ip_address 服务 IP 地址
 * @param port 服务端口
 * @param max_retries 最大重试次数（0 表示无限重试）
 * @param retry_interval_ms 重试间隔（毫秒）
 * @param running 运行状态标志
 * @return 连接是否成功
 */
template<typename ClientType>
bool connectWithRetry(
    std::shared_ptr<ClientType> client,
    const std::string& service_name,
    const std::string& ip_address,
    uint16_t port,
    uint32_t max_retries,
    uint32_t retry_interval_ms,
    const std::atomic<bool>& running)
{
    uint32_t attempt = 0;
    
    while (running) {
        attempt++;
        FotaLogAdapter::orchestrator().info(
            "fota.service.connecting",
            "Connecting to service",
            {flog::f_str("service_name", service_name),
             flog::f_str("ip_address", ip_address),
             flog::f_int("port", port),
             flog::f_int("attempt", attempt)}
        );
        
        if (client->connect(ip_address, port)) {
            FotaLogAdapter::orchestrator().info(
                "fota.service.connected",
                "Connected to service successfully",
                {flog::f_str("service_name", service_name),
                 flog::f_int("attempt", attempt)}
            );
            return true;
        }
        
        FotaLogAdapter::orchestrator().warn(
            "fota.service.connect_failed",
            "Connection failed",
            {flog::f_str("service_name", service_name),
             flog::f_int("attempt", attempt)}
        );
        
        // 检查是否达到最大重试次数
        if (max_retries > 0 && attempt >= max_retries) {
            FotaLogAdapter::orchestrator().error(
                "fota.service.connect_exhausted",
                "Max retries reached",
                {flog::f_str("service_name", service_name),
                 flog::f_int("max_retries", max_retries)}
            );
            return false;
        }
        
        // 分段睡眠以便响应停止信号
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(retry_interval_ms);
        while (running && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    
    return false;
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
    // 任一不可读文件 / YAML 错误 / 类型错误 / 越界值 / 未知字段均终止服务开放。
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
    // 启动顺序：Config -> Logger -> Store::open -> 迁移 -> 恢复 -> 业务模块 -> SOME/IP -> 自动任务
    // 恢复在开放 SOME/IP 服务和自动采集前完成。
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

    // Create SOME/IP clients
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // Configure service IDs (寻址过渡 SSOT: constants.h, CGW-FOTA-DSN-CR-004)
    diag_client->setServiceId(DEFAULT_DIAG_SERVICE_ID);
    diag_client->setInstanceId(DEFAULT_DIAG_INSTANCE_ID);

    // 并行连接 CGW-DIAG 和 TBOX-SOMEIP 服务
    std::atomic<bool> diag_connected{false};
    std::atomic<bool> tbox_connected{false};
    std::atomic<bool> running_flag{true};

    FotaLogAdapter::orchestrator().info("fota.service.connecting_services",
        "Connecting to services in parallel..."
    );

    // 启动 CGW-DIAG 连接线程
    std::thread diag_thread([&]() {
        diag_connected = connectWithRetry(
            diag_client,
            "CGW-DIAG",
            DEFAULT_DIAG_IP_ADDRESS,
            DEFAULT_DIAG_PORT,
            DEFAULT_MAX_RETRY_COUNT,
            DEFAULT_RETRY_INTERVAL_MS,
            running_flag
        );
    });

    // 启动 TBOX-SOMEIP 连接线程
    std::thread tbox_thread([&]() {
        tbox_connected = connectWithRetry(
            tbox_client,
            "TBOX-SOMEIP",
            DEFAULT_TBOX_IP_ADDRESS,
            DEFAULT_TBOX_PORT,
            DEFAULT_MAX_RETRY_COUNT,
            DEFAULT_RETRY_INTERVAL_MS,
            running_flag
        );
    });

    // 等待两个连接线程完成
    diag_thread.join();
    tbox_thread.join();

    // 检查连接结果
    if (!diag_connected || !tbox_connected) {
        FotaLogAdapter::orchestrator().error("fota.service.connect_failed",
            "Failed to connect to required services",
            {flog::f_bool("diag_connected", diag_connected.load()),
             flog::f_bool("tbox_connected", tbox_connected.load())}
        );
        
        // 清理已连接的客户端
        if (diag_connected) diag_client->disconnect();
        if (tbox_connected) tbox_client->disconnect();
        return 1;
    }

    FotaLogAdapter::orchestrator().info("fota.service.all_connected",
        "All services connected successfully"
    );

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(static_cast<uint32_t>(fotaConfig.minReportInterval.count()));
    assembler->setMaxEcuCount(DEFAULT_MAX_ECU_COUNT);
    assembler->setStateStore(state_store);  // CGW-FOTA-DSN-CR-005: durable 序号分配

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(fotaConfig.tboxRetry.maxAttempts,
                             static_cast<uint32_t>(fotaConfig.tboxRetry.backoff.count()));
    reporter->setDedupWindowSize(DEFAULT_DEDUP_WINDOW_SIZE);
    reporter->setStateStore(state_store);  // CGW-FOTA-DSN-CR-005: 检查点/去重/成功快照
    reporter->applyRecoveryPlan(recovery_plan);  // 恢复在途任务

    // Create and start FOTA Provider (CGW-FOTA-DSN-CR-002)
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    
    FotaLogAdapter::orchestrator().info("fota.service.provider_starting",
        "Starting FOTA Provider",
        {flog::f_str("ip_address", FOTA_PROVIDER_IP_ADDRESS),
         flog::f_int("port", FOTA_PROVIDER_PORT),
         flog::f_str("service_id", hex_id(FOTA_PROVIDER_SERVICE_ID))}
    );

    if (!provider->start(FOTA_PROVIDER_IP_ADDRESS, FOTA_PROVIDER_PORT)) {
        FotaLogAdapter::orchestrator().error("fota.service.provider_start_failed",
            "Failed to start FOTA Provider"
        );
        diag_client->disconnect();
        tbox_client->disconnect();
        return 1;
    }

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_READY,
        "CGW-FOTA Service started successfully"
    );

    // Initial report (auto-trigger: generates independent trace_id/request_id)
    // CGW-FOTA-DSN-CR-004: 由 fota.inventory.auto_report_on_start 控制
    // CGW-FOTA-DSN-CR-005: 恢复阻断时跳过自动上报
    if (recovery_plan.action == cgw_fota::store::RecoveryAction::Blocked) {
        FotaLogAdapter::orchestrator().info("fota.service.initial_report_skipped",
            "Initial auto report skipped due to blocked recovery"
        );
    } else if (fotaConfig.autoReportOnStart) {
        FotaLogAdapter::orchestrator().info("fota.service.initial_report_starting",
            "Performing initial inventory report"
        );

        {
            // Auto-trigger generates independent context (CGW-FOTA-DSN-CR-003 §上下文传播)
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

    FotaLogAdapter::orchestrator().info(fota_events::SERVICE_SHUTTING_DOWN,
        "Stopping CGW-FOTA Service..."
    );

    // Main loop - in real implementation, this would handle events
    while (running) {
        sleep(1);
    }

    // Cleanup
    provider->stop();
    diag_client->disconnect();
    tbox_client->disconnect();

    FotaLogAdapter::orchestrator().info("fota.service.stopped",
        "CGW-FOTA Service stopped"
    );

    return 0;
}
