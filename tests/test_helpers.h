#pragma once

#include "cgw/fota/someip/diag_inventory_client.hpp"
#include "cgw/fota/someip/tbox_inventory_client.hpp"
#include "cgw/fota/someip/fota_provider.hpp"
#include "inventory_reporter.h"
#include "data_models.h"

namespace cgw_fota {
namespace test {

// CGW-FOTA-DSN-CR-007: 测试子类继承 framework-backed 适配器，override 业务方法
// 以避免真实 SOME/IP 调用（适配器的 protected 默认构造不持有 framework Client）。

// Testable DIAG 采集 Client：override collectVehicleInventory 直接返回测试快照。
class TestableDiagInventoryClient : public someip::DiagInventoryClient {
public:
    TestableDiagInventoryClient() : DiagInventoryClient() {}

    bool collectVehicleInventory(VehicleSoftwareSnapshot& snapshot) override {
        if (test_vin_.empty()) {
            return false;
        }
        snapshot.vin = test_vin_;
        snapshot.baseline_id = "BASELINE001";
        snapshot.baseline_source = BaselineSource::FACTORY;
        snapshot.registry_version = "1.0.0";
        snapshot.collected_at = "2026-07-21T10:00:00Z";
        snapshot.overall_result = CollectionStatus::ALL_OK;
        snapshot.snapshot_seq = 1;

        EcuVersionEntry entry1;
        entry1.ecu_id = "ECU001";
        entry1.part_number = "PN001";
        entry1.sw_version = "1.0.0";
        entry1.hw_version = "HW1.0";
        entry1.source = VersionSource::UDS_0x22;
        entry1.status = EcuStatus::OK;
        snapshot.ecu_list = {entry1};
        return true;
    }

    bool getVin(std::string& vin) override {
        if (test_vin_.empty()) return false;
        vin = test_vin_;
        return true;
    }

    void setTestVin(const std::string& vin) { test_vin_ = vin; }

private:
    std::string test_vin_ = "12345678901234567";
};

// Testable TBOX 提交 Client：override reportSoftwareInventory 记录上报。
class TestableTboxInventoryClient : public someip::TboxInventoryClient {
public:
    TestableTboxInventoryClient() : TboxInventoryClient() {}

    bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot) override {
        last_reported_vin_ = snapshot.vin;
        last_reported_seq_ = snapshot.snapshot_seq;
        report_count_++;
        return !fail_next_;
    }

    void setFailNext(bool fail) { fail_next_ = fail; }

    const std::string& getLastReportedVin() const { return last_reported_vin_; }
    uint64_t getLastReportedSeq() const { return last_reported_seq_; }
    int getReportCount() const { return report_count_; }

private:
    std::string last_reported_vin_;
    uint64_t last_reported_seq_ = 0;
    int report_count_ = 0;
    bool fail_next_ = false;
};

// Testable FOTA Provider：注入 reporter 但不持有 framework Provider。
// handleRequest 可用（委托 orchestrator）；offer/stopOffer 为 no-op。
class TestableFotaProviderAdapter : public someip::FotaProviderAdapter {
public:
    TestableFotaProviderAdapter(std::shared_ptr<InventoryReporter> reporter,
                                std::chrono::milliseconds acceptBudget =
                                    std::chrono::milliseconds(1000))
        : FotaProviderAdapter(reporter, acceptBudget) {}
};

} // namespace test
} // namespace cgw_fota
