#pragma once

#include "data_models.h"
#include "inventory_reporter.h"
#include <string>
#include <memory>
#include <functional>

namespace cgw_fota {

/**
 * CGW-FOTA SOME/IP Provider (CGW-FOTA-DSN-CR-002)
 * 
 * 监听 TBOX-TSP 的入站请求，处理 METHOD_REQUEST_SOFTWARE_INVENTORY
 * Service ID: 0x1120, Instance ID: 0x0001, TCP Port: 51120
 */
class SomeIpFotaProvider {
public:
    SomeIpFotaProvider(std::shared_ptr<InventoryReporter> reporter);
    ~SomeIpFotaProvider();

    /**
     * 启动 Provider 监听
     * @param ip_address 监听地址，通常为 "0.0.0.0"
     * @param port 监听端口，通常为 51120
     * @return true 启动成功
     */
    bool start(const std::string& ip_address, uint16_t port);

    /**
     * 停止 Provider
     */
    bool stop();

    /**
     * 检查 Provider 是否正在运行
     */
    bool isRunning() const;

    /**
     * 处理 METHOD_REQUEST_SOFTWARE_INVENTORY 请求（用于测试）
     * @param request_id 请求 ID
     * @param reason 请求原因
     * @return AsyncReportResult 受理结果
     */
    AsyncReportResult handleRequest(const std::string& request_id, const std::string& reason);

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    std::shared_ptr<InventoryReporter> reporter_;
};

} // namespace cgw_fota
