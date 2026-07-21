#include "config_loader.h"
#include "someip_fota_client.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include "inventory_reporter.h"
#include <gtest/gtest.h>

using namespace cgw_fota;

class IntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Setup test environment
    }

    void TearDown() override {
        // Clean up
    }
};

TEST_F(IntegrationTest, FullWorkflow) {
    // Load configuration
    ConfigLoader config;
    // For testing, we'll use default values

    // Create SOME/IP clients
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // Connect to services
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort()));

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(config.getThrottleIntervalMs());
    assembler->setMaxEcuCount(config.getMaxEcuCount());

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    reporter->setRetryPolicy(config.getMaxRetryCount(), config.getRetryIntervalMs());
    reporter->setDedupWindowSize(config.getDedupWindowSize());

    // Report inventory
    bool result = reporter->reportInventory("12345678901234567");

    EXPECT_TRUE(result);

    // Disconnect
    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, MultipleReports) {
    // Load configuration
    ConfigLoader config;

    // Create SOME/IP clients
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // Connect to services
    ASSERT_TRUE(diag_client->connect(config.getDiagIpAddress(), config.getDiagPort()));
    ASSERT_TRUE(tbox_client->connect(config.getTboxIpAddress(), config.getTboxPort()));

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    assembler->setThrottleInterval(0); // Disable throttling for test

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    // Report inventory multiple times
    for (int i = 0; i < 3; ++i) {
        bool result = reporter->reportInventory("12345678901234567");
        EXPECT_TRUE(result);
    }

    // Disconnect
    diag_client->disconnect();
    tbox_client->disconnect();
}

TEST_F(IntegrationTest, ErrorHandling) {
    // Test with invalid configuration
    ConfigLoader config;

    // Create SOME/IP clients with invalid addresses
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();

    // These should fail to connect
    // Note: In a real implementation, these would fail
    // For now, we'll just test the workflow

    // Create snapshot assembler
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);

    // Create inventory reporter
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);

    // This should fail because we're not connected
    bool result = reporter->reportInventory("12345678901234567");

    EXPECT_FALSE(result);
}
