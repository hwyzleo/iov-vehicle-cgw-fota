#include "config_loader.h"
#include "someip_fota_client.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "someip_fota_provider.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <chrono>

using namespace cgw_fota;

volatile bool running = true;

void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
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
        std::cout << "[" << service_name << "] Connecting to " << ip_address << ":" << port
                  << " (attempt " << attempt << ")" << std::endl;
        
        if (client->connect(ip_address, port)) {
            std::cout << "[" << service_name << "] Connected successfully" << std::endl;
            return true;
        }
        
        std::cerr << "[" << service_name << "] Connection failed" << std::endl;
        
        // 检查是否达到最大重试次数
        if (max_retries > 0 && attempt >= max_retries) {
            std::cerr << "[" << service_name << "] Max retries (" << max_retries << ") reached" << std::endl;
            return false;
        }
        
        std::cout << "[" << service_name << "] Retrying in " << retry_interval_ms << "ms..." << std::endl;
        
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

    std::cout << "Starting CGW-FOTA Service..." << std::endl;

    // Load configuration
    ConfigLoader config;
    std::string config_path = "config/fota_config.yaml";

    if (argc > 1) {
        config_path = argv[1];
    }

    if (!config.loadConfig(config_path)) {
        std::cerr << "Failed to load configuration from: " << config_path << std::endl;
        return 1;
    }

    std::cout << "Configuration loaded successfully" << std::endl;

    // Create SOME/IP clients
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // Configure service IDs from config
    diag_client->setServiceId(config.getDiagServiceId());
    diag_client->setInstanceId(config.getDiagInstanceId());

    // 并行连接 CGW-DIAG 和 TBOX-SOMEIP 服务
    std::atomic<bool> diag_connected{false};
    std::atomic<bool> tbox_connected{false};
    std::atomic<bool> running_flag{true};

    std::cout << "Connecting to services in parallel..." << std::endl;

    // 启动 CGW-DIAG 连接线程
    std::thread diag_thread([&]() {
        diag_connected = connectWithRetry(
            diag_client,
            "CGW-DIAG",
            config.getDiagIpAddress(),
            config.getDiagPort(),
            config.getMaxRetryCount(),
            config.getRetryIntervalMs(),
            running_flag
        );
    });

    // 启动 TBOX-SOMEIP 连接线程
    std::thread tbox_thread([&]() {
        tbox_connected = connectWithRetry(
            tbox_client,
            "TBOX-SOMEIP",
            config.getTboxIpAddress(),
            config.getTboxPort(),
            config.getMaxRetryCount(),
            config.getRetryIntervalMs(),
            running_flag
        );
    });

    // 等待两个连接线程完成
    diag_thread.join();
    tbox_thread.join();

    // 检查连接结果
    if (!diag_connected || !tbox_connected) {
        std::cerr << "Failed to connect to required services:" << std::endl;
        if (!diag_connected) {
            std::cerr << "  - CGW-DIAG: FAILED" << std::endl;
        } else {
            std::cout << "  - CGW-DIAG: OK" << std::endl;
        }
        if (!tbox_connected) {
            std::cerr << "  - TBOX-SOMEIP: FAILED" << std::endl;
        } else {
            std::cout << "  - TBOX-SOMEIP: OK" << std::endl;
        }
        
        // 清理已连接的客户端
        if (diag_connected) diag_client->disconnect();
        if (tbox_connected) tbox_client->disconnect();
        return 1;
    }

    std::cout << "All services connected successfully" << std::endl;

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(config.getThrottleIntervalMs());
    assembler->setMaxEcuCount(config.getMaxEcuCount());

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(config.getMaxRetryCount(), config.getRetryIntervalMs());
    reporter->setDedupWindowSize(config.getDedupWindowSize());

    // Create and start FOTA Provider (CGW-FOTA-DSN-CR-002)
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    
    std::cout << "Starting FOTA Provider on "
              << config.getFotaProviderIpAddress() << ":" << config.getFotaProviderPort()
              << " (service_id=0x" << std::hex << config.getFotaProviderServiceId() << std::dec << ")" << std::endl;

    if (!provider->start(config.getFotaProviderIpAddress(), config.getFotaProviderPort())) {
        std::cerr << "Failed to start FOTA Provider" << std::endl;
        diag_client->disconnect();
        tbox_client->disconnect();
        return 1;
    }

    std::cout << "CGW-FOTA Service started successfully" << std::endl;
    std::cout << "Press Ctrl+C to stop" << std::endl;

    // Initial report (VIN comes from DIAG)
    std::cout << "Performing initial inventory report..." << std::endl;

    if (reporter->reportInventory()) {
        std::cout << "Initial inventory report successful" << std::endl;
    } else {
        std::cerr << "Initial inventory report failed" << std::endl;
    }

    // Main loop - in real implementation, this would handle events
    while (running) {
        // Simulate event handling
        sleep(1);

        // In real implementation, this would be event-driven
        // For now, we'll just keep the service running
    }

    std::cout << "Stopping CGW-FOTA Service..." << std::endl;

    // Cleanup
    provider->stop();
    diag_client->disconnect();
    tbox_client->disconnect();

    std::cout << "CGW-FOTA Service stopped" << std::endl;

    return 0;
}
