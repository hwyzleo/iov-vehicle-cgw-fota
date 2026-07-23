#include "config_loader.h"
#include "someip_fota_client.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include "someip_fota_provider.h"
#include <iostream>
#include <signal.h>
#include <unistd.h>

using namespace cgw_fota;

volatile bool running = true;

void signalHandler(int signum) {
    std::cout << "Interrupt signal (" << signum << ") received.\n";
    running = false;
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

    // Connect to services
    std::cout << "Connecting to CGW-DIAG service at "
              << config.getDiagIpAddress() << ":" << config.getDiagPort()
              << " (service_id=0x" << std::hex << config.getDiagServiceId() << std::dec << ")" << std::endl;

    if (!diag_client->connect(config.getDiagIpAddress(), config.getDiagPort())) {
        std::cerr << "Failed to connect to CGW-DIAG service" << std::endl;
        return 1;
    }

    std::cout << "Connecting to TBOX service at "
              << config.getTboxIpAddress() << ":" << config.getTboxPort() << std::endl;

    if (!tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort())) {
        std::cerr << "Failed to connect to TBOX service" << std::endl;
        diag_client->disconnect();
        return 1;
    }

    std::cout << "Connected to services successfully" << std::endl;

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
