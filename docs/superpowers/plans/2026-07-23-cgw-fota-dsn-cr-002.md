# CGW-FOTA-DSN-CR-002 实现计划

> **面向 AI 代理的工作者：** 必需子技能：使用 superpowers:subagent-driven-development（推荐）或 superpowers:executing-plans 逐任务实现此计划。步骤使用复选框（`- [ ]`）语法来跟踪进度。

**目标：** 为 CGW-FOTA 添加 SOME/IP Provider 接口，支持 TBOX-TSP 主动请求软件版本采集上报

**架构：** 新增 SomeIpFotaProvider 类（TCP Server）监听 51120 端口，处理 METHOD_REQUEST_SOFTWARE_INVENTORY 请求；增强 InventoryReporter 支持异步采集和并发控制；更新 TBOX Service ID 为 0x6101

**技术栈：** C++17, Google Test, Google Mock, PImpl 模式

---

## 文件结构

### 新建文件
| 文件路径 | 职责 |
|---------|------|
| `include/someip_fota_provider.h` | Provider 类声明 |
| `src/someip_fota_provider.cpp` | Provider 实现（PImpl） |
| `tests/test_someip_fota_provider.cpp` | Provider 单元测试 |

### 修改文件
| 文件路径 | 变更内容 |
|---------|---------|
| `include/constants.h` | 新增 Provider 常量，更新 TBOX 常量 |
| `include/inventory_reporter.h` | 新增异步方法和并发控制成员 |
| `src/inventory_reporter.cpp` | 实现异步采集和并发控制 |
| `config/fota_config.yaml` | 新增 Provider 配置，更新 TBOX 配置 |
| `include/config_loader.h` | 新增 Provider 配置 getter |
| `src/config_loader.cpp` | 实现 Provider 配置加载 |
| `src/main.cpp` | 集成 Provider 启动流程 |
| `tests/test_inventory_reporter.cpp` | 新增异步和并发测试 |
| `tests/test_integration.cpp` | 新增端到端测试 |

---

## 任务 1：更新常量定义

**文件：**
- 修改：`include/constants.h`
- 测试：无（纯常量定义）

- [ ] **步骤 1：添加 FOTA Provider 常量**

```cpp
// 在 constants.h 中添加

// FOTA Provider 服务 (CGW-FOTA-DSN-CR-002)
constexpr uint16_t FOTA_PROVIDER_SERVICE_ID = 0x1120;
constexpr uint16_t FOTA_PROVIDER_INSTANCE_ID = 0x0001;
constexpr uint16_t FOTA_PROVIDER_PORT = 51120;

// FOTA Provider Method IDs (service-scoped)
constexpr uint16_t METHOD_REQUEST_SOFTWARE_INVENTORY = 0x0001;
```

- [ ] **步骤 2：更新 TBOX 服务常量**

```cpp
// 修改 constants.h 中的 TBOX 常量

// TBOX 服务 (CGW-FOTA-DSN-CR-002 更新)
constexpr uint16_t DEFAULT_TBOX_SERVICE_ID = 0x6101;  // 原 0x0002
constexpr uint16_t DEFAULT_TBOX_INSTANCE_ID = 0x0001;
constexpr uint16_t DEFAULT_TBOX_PORT = 56101;  // 新增端口常量
```

- [ ] **步骤 3：验证编译**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota && mkdir -p build && cd build && cmake .. && make`
预期：编译成功，无错误

- [ ] **步骤 4：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add include/constants.h
git commit -m "feat(cr-002): add FOTA Provider and update TBOX service constants"
```

---

## 任务 2：更新配置文件

**文件：**
- 修改：`config/fota_config.yaml`
- 测试：无（配置文件）

- [ ] **步骤 1：添加 FOTA Provider 配置**

在 `config/fota_config.yaml` 的 `someip:` 部分添加：

```yaml
    # FOTA Provider 服务 (CGW-FOTA-DSN-CR-002)
    # Service ID: 0x1120, Instance ID: 0x0001, Transport: TCP, Port: 51120
    fota_provider:
      service_id: 0x1120
      instance_id: 0x0001
      ip_address: "0.0.0.0"
      port: 51120
```

- [ ] **步骤 2：更新 TBOX 服务配置**

修改 `config/fota_config.yaml` 中的 `tbox_service:` 部分：

```yaml
    # TBOX 服务 (CGW-FOTA-DSN-CR-002 更新)
    # Service ID: 0x6101, Instance ID: 0x0001, Transport: TCP, Port: 56101
    tbox_service:
      service_id: 0x6101  # 原 0x0002
      instance_id: 0x0001
      ip_address: "127.0.0.1"
      port: 56101  # 原 30502
```

- [ ] **步骤 3：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add config/fota_config.yaml
git commit -m "feat(cr-002): add FOTA Provider and update TBOX configuration"
```

---

## 任务 3：更新 ConfigLoader

**文件：**
- 修改：`include/config_loader.h`
- 修改：`src/config_loader.cpp`
- 测试：`tests/test_config_loader.cpp`

- [ ] **步骤 1：在 config_loader.h 添加 Provider 配置 getter 声明**

在 `include/config_loader.h` 的 public 部分添加：

```cpp
    // FOTA Provider configuration (CGW-FOTA-DSN-CR-002)
    uint16_t getFotaProviderServiceId() const;
    uint16_t getFotaProviderInstanceId() const;
    std::string getFotaProviderIpAddress() const;
    uint16_t getFotaProviderPort() const;
```

在 private 部分添加成员变量：

```cpp
    // FOTA Provider configuration
    uint16_t fota_provider_service_id_;
    uint16_t fota_provider_instance_id_;
    std::string fota_provider_ip_address_;
    uint16_t fota_provider_port_;
```

- [ ] **步骤 2：在 config_loader.cpp 添加实现**

在 `src/config_loader.cpp` 中：

1. 在构造函数初始化列表添加：

```cpp
    , fota_provider_service_id_(FOTA_PROVIDER_SERVICE_ID)
    , fota_provider_instance_id_(FOTA_PROVIDER_INSTANCE_ID)
    , fota_provider_ip_address_("0.0.0.0")
    , fota_provider_port_(FOTA_PROVIDER_PORT)
```

2. 在 `loadConfig()` 函数中添加解析逻辑（参照 diag_service 的实现）：

```cpp
    // Load FOTA Provider configuration
    if (config["someip"]["fota_provider"]) {
        auto provider = config["someip"]["fota_provider"];
        if (provider["service_id"]) {
            fota_provider_service_id_ = provider["service_id"].as<uint16_t>();
        }
        if (provider["instance_id"]) {
            fota_provider_instance_id_ = provider["instance_id"].as<uint16_t>();
        }
        if (provider["ip_address"]) {
            fota_provider_ip_address_ = provider["ip_address"].as<std::string>();
        }
        if (provider["port"]) {
            fota_provider_port_ = provider["port"].as<uint16_t>();
        }
    }
```

3. 添加 getter 实现：

```cpp
uint16_t ConfigLoader::getFotaProviderServiceId() const {
    return fota_provider_service_id_;
}

uint16_t ConfigLoader::getFotaProviderInstanceId() const {
    return fota_provider_instance_id_;
}

std::string ConfigLoader::getFotaProviderIpAddress() const {
    return fota_provider_ip_address_;
}

uint16_t ConfigLoader::getFotaProviderPort() const {
    return fota_provider_port_;
}
```

- [ ] **步骤 3：更新 TBOX 配置解析**

修改 `loadConfig()` 中 TBOX 配置解析，添加 port 字段：

```cpp
    if (config["someip"]["tbox_service"]["port"]) {
        tbox_port_ = config["someip"]["tbox_service"]["port"].as<uint16_t>();
    }
```

- [ ] **步骤 4：添加单元测试**

在 `tests/test_config_loader.cpp` 添加测试：

```cpp
TEST_F(ConfigLoaderTest, LoadFotaProviderConfig) {
    ConfigLoader config;
    bool result = config.loadConfig("config/fota_config.yaml");

    EXPECT_TRUE(result);
    EXPECT_EQ(config.getFotaProviderServiceId(), 0x1120);
    EXPECT_EQ(config.getFotaProviderInstanceId(), 0x0001);
    EXPECT_EQ(config.getFotaProviderIpAddress(), "0.0.0.0");
    EXPECT_EQ(config.getFotaProviderPort(), 51120);
}

TEST_F(ConfigLoaderTest, LoadUpdatedTboxConfig) {
    ConfigLoader config;
    bool result = config.loadConfig("config/fota_config.yaml");

    EXPECT_TRUE(result);
    EXPECT_EQ(config.getTboxServiceId(), 0x6101);
    EXPECT_EQ(config.getTboxPort(), 56101);
}
```

- [ ] **步骤 5：运行测试验证**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && make && ./tests/test_config_loader`
预期：所有测试通过

- [ ] **步骤 6：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add include/config_loader.h src/config_loader.cpp tests/test_config_loader.cpp
git commit -m "feat(cr-002): add ConfigLoader support for FOTA Provider"
```

---

## 任务 4：增强 InventoryReporter 并发控制

**文件：**
- 修改：`include/inventory_reporter.h`
- 修改：`src/inventory_reporter.cpp`
- 测试：`tests/test_inventory_reporter.cpp`

- [ ] **步骤 1：在 inventory_reporter.h 添加异步结构和方法声明**

在 `include/inventory_reporter.h` 中添加：

```cpp
// 在类定义之前添加结构体
struct AsyncReportResult {
    bool accepted;
    uint64_t report_id;
};

// 在类的 public 部分添加
    /**
     * 异步触发采集上报，返回受理结果
     * @param request_id 请求 ID，用于日志追踪
     * @param reason 请求原因 (cloud_query, manual_retry, integration_test)
     * @return AsyncReportResult 包含是否受理和 reportId
     */
    AsyncReportResult reportInventoryAsync(const std::string& request_id, const std::string& reason);

    /**
     * 检查是否有在途采集任务
     */
    bool isCollecting() const;

    /**
     * 获取当前在途任务的 reportId
     */
    uint64_t getCurrentReportId() const;

// 在类的 private 部分添加成员变量
    std::atomic<bool> is_collecting_{false};
    std::atomic<uint64_t> report_seq_{0};
    std::atomic<uint64_t> current_report_id_{0};
```

- [ ] **步骤 2：在 inventory_reporter.cpp 实现异步方法**

在 `src/inventory_reporter.cpp` 中添加实现：

```cpp
AsyncReportResult InventoryReporter::reportInventoryAsync(const std::string& request_id, 
                                                         const std::string& reason) {
    // 检查是否有在途任务
    if (is_collecting_.load()) {
        // 合并到在途任务
        uint64_t existing_report_id = current_report_id_.load();
        std::cout << "Merging request " << request_id 
                  << " to in-flight task, reportId=" << existing_report_id << std::endl;
        return {true, existing_report_id};
    }

    // 分配新的 reportId
    uint64_t new_report_id = ++report_seq_;
    is_collecting_.store(true);
    current_report_id_.store(new_report_id);

    std::cout << "Accepted request " << request_id 
              << ", reason=" << reason 
              << ", reportId=" << new_report_id << std::endl;

    // 异步启动采集上报（在独立线程中执行）
    std::thread([this, new_report_id]() {
        try {
            // 执行同步的采集上报
            bool result = this->reportInventory();
            
            if (result) {
                std::cout << "Async report completed, reportId=" << new_report_id << std::endl;
            } else {
                std::cerr << "Async report failed, reportId=" << new_report_id << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Async report exception: " << e.what() 
                      << ", reportId=" << new_report_id << std::endl;
        }

        // 清除在途标记
        is_collecting_.store(false);
    }).detach();

    return {true, new_report_id};
}

bool InventoryReporter::isCollecting() const {
    return is_collecting_.load();
}

uint64_t InventoryReporter::getCurrentReportId() const {
    return current_report_id_.load();
}
```

- [ ] **步骤 3：添加并发控制测试**

在 `tests/test_inventory_reporter.cpp` 添加测试：

```cpp
TEST_F(InventoryReporterTest, AsyncReportReturnsImmediateResult) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    VehicleSoftwareSnapshot snapshot;
    snapshot.vin = "12345678901234567";
    snapshot.snapshot_seq = 1;

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([&snapshot](VehicleSoftwareSnapshot& s) {
            s = snapshot;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 第一个请求应该被接受
    auto result1 = reporter.reportInventoryAsync("req-001", "cloud_query");
    EXPECT_TRUE(result1.accepted);
    EXPECT_EQ(result1.report_id, 1);

    // 等待一小段时间让异步任务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 第二个请求应该合并到在途任务
    auto result2 = reporter.reportInventoryAsync("req-002", "manual_retry");
    EXPECT_TRUE(result2.accepted);
    EXPECT_EQ(result2.report_id, 1);  // 相同的 reportId

    // 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

TEST_F(InventoryReporterTest, IsCollectingState) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
            s.vin = "12345678901234567";
            s.snapshot_seq = 1;
            // 模拟耗时操作
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 初始状态不在采集
    EXPECT_FALSE(reporter.isCollecting());

    // 触发异步采集
    reporter.reportInventoryAsync("req-001", "cloud_query");

    // 应该立即变为采集状态
    EXPECT_TRUE(reporter.isCollecting());

    // 等待完成
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // 完成后应该不在采集
    EXPECT_FALSE(reporter.isCollecting());
}

TEST_F(InventoryReporterTest, ReportIdIncrement) {
    auto mock_tbox_client = std::make_shared<MockSomeIpTboxClient>();
    auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

    EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
        .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
            s.vin = "12345678901234567";
            s.snapshot_seq = 1;
            return true;
        }));

    EXPECT_CALL(*mock_tbox_client, reportSoftwareInventory(_))
        .WillRepeatedly(Return(true));

    InventoryReporter reporter(mock_tbox_client, mock_assembler);

    // 触发多个请求（等待前一个完成）
    auto result1 = reporter.reportInventoryAsync("req-001", "cloud_query");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto result2 = reporter.reportInventoryAsync("req-002", "manual_retry");
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // reportId 应该递增
    EXPECT_EQ(result1.report_id, 1);
    EXPECT_EQ(result2.report_id, 2);
}
```

- [ ] **步骤 4：运行测试验证**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && make && ./tests/test_inventory_reporter`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add include/inventory_reporter.h src/inventory_reporter.cpp tests/test_inventory_reporter.cpp
git commit -m "feat(cr-002): add async report and concurrent control to InventoryReporter"
```

---

## 任务 5：创建 SomeIpFotaProvider 类

**文件：**
- 创建：`include/someip_fota_provider.h`
- 创建：`src/someip_fota_provider.cpp`
- 测试：`tests/test_someip_fota_provider.cpp`

- [ ] **步骤 1：创建 someip_fota_provider.h 头文件**

创建 `include/someip_fota_provider.h`：

```cpp
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

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    std::shared_ptr<InventoryReporter> reporter_;

    /**
     * 处理 METHOD_REQUEST_SOFTWARE_INVENTORY 请求
     * @param request_id 请求 ID
     * @param reason 请求原因
     * @return AsyncReportResult 受理结果
     */
    AsyncReportResult handleRequest(const std::string& request_id, const std::string& reason);
};

} // namespace cgw_fota
```

- [ ] **步骤 2：创建 someip_fota_provider.cpp 实现**

创建 `src/someip_fota_provider.cpp`：

```cpp
#include "someip_fota_provider.h"
#include "constants.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>

namespace cgw_fota {

// SOME/IP 消息头结构
struct SomeIpHeader {
    uint16_t service_id;
    uint16_t method_id;
    uint32_t length;
    uint16_t client_id;
    uint16_t session_id;
    uint8_t protocol_version;
    uint8_t interface_version;
    uint8_t message_type;
    uint8_t return_code;
};

class SomeIpFotaProvider::Impl {
public:
    Impl(SomeIpFotaProvider* parent) : parent_(parent), running_(false), server_fd_(-1) {}

    ~Impl() {
        stop();
    }

    bool start(const std::string& ip_address, uint16_t port) {
        if (running_.load()) {
            std::cerr << "Provider already running" << std::endl;
            return false;
        }

        // 创建 socket
        server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
        if (server_fd_ < 0) {
            std::cerr << "Failed to create socket" << std::endl;
            return false;
        }

        // 设置 socket 选项
        int opt = 1;
        if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            std::cerr << "Failed to set socket options" << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        // 绑定地址
        struct sockaddr_in address;
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = inet_addr(ip_address.c_str());
        address.sin_port = htons(port);

        if (bind(server_fd_, (struct sockaddr*)&address, sizeof(address)) < 0) {
            std::cerr << "Failed to bind to " << ip_address << ":" << port << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        // 开始监听
        if (listen(server_fd_, 3) < 0) {
            std::cerr << "Failed to listen on socket" << std::endl;
            close(server_fd_);
            server_fd_ = -1;
            return false;
        }

        running_.store(true);
        
        // 启动接受连接的线程
        accept_thread_ = std::thread([this]() {
            acceptConnections();
        });

        std::cout << "FOTA Provider started on " << ip_address << ":" << port << std::endl;
        return true;
    }

    bool stop() {
        if (!running_.load()) {
            return true;
        }

        running_.store(false);

        if (server_fd_ >= 0) {
            close(server_fd_);
            server_fd_ = -1;
        }

        if (accept_thread_.joinable()) {
            accept_thread_.join();
        }

        std::cout << "FOTA Provider stopped" << std::endl;
        return true;
    }

    bool isRunning() const {
        return running_.load();
    }

private:
    SomeIpFotaProvider* parent_;
    std::atomic<bool> running_;
    int server_fd_;
    std::thread accept_thread_;

    void acceptConnections() {
        while (running_.load()) {
            struct sockaddr_in client_address;
            socklen_t client_len = sizeof(client_address);

            int client_fd = accept(server_fd_, (struct sockaddr*)&client_address, &client_len);
            if (client_fd < 0) {
                if (running_.load()) {
                    std::cerr << "Failed to accept connection" << std::endl;
                }
                continue;
            }

            // 为每个连接启动处理线程
            std::thread([this, client_fd]() {
                handleConnection(client_fd);
            }).detach();
        }
    }

    void handleConnection(int client_fd) {
        // 读取 SOME/IP 消息头
        SomeIpHeader header;
        ssize_t bytes_read = read(client_fd, &header, sizeof(header));
        
        if (bytes_read != sizeof(header)) {
            std::cerr << "Failed to read SOME/IP header" << std::endl;
            close(client_fd);
            return;
        }

        // 验证 Service ID
        if (ntohs(header.service_id) != FOTA_PROVIDER_SERVICE_ID) {
            std::cerr << "Invalid service ID: 0x" << std::hex << ntohs(header.service_id) << std::endl;
            close(client_fd);
            return;
        }

        // 路由到对应的处理方法
        uint16_t method_id = ntohs(header.method_id);
        
        if (method_id == METHOD_REQUEST_SOFTWARE_INVENTORY) {
            handleRequestSoftwareInventory(client_fd, header);
        } else {
            std::cerr << "Unknown method ID: 0x" << std::hex << method_id << std::endl;
            close(client_fd);
        }
    }

    void handleRequestSoftwareInventory(int client_fd, const SomeIpHeader& request_header) {
        // 读取请求负载
        uint32_t payload_length = ntohl(request_header.length) - sizeof(SomeIpHeader);
        std::vector<uint8_t> payload(payload_length);
        
        ssize_t bytes_read = read(client_fd, payload.data(), payload_length);
        if (bytes_read != payload_length) {
            std::cerr << "Failed to read request payload" << std::endl;
            close(client_fd);
            return;
        }

        // 解析 requestId 和 reason（简化实现）
        // 实际实现应使用 Protobuf
        std::string request_id = "req-" + std::to_string(request_header.session_id);
        std::string reason = "cloud_query";

        // 调用 parent 的 handleRequest
        AsyncReportResult result = parent_->handleRequest(request_id, reason);

        // 构建响应
        SomeIpHeader response_header;
        response_header.service_id = request_header.service_id;
        response_header.method_id = request_header.method_id;
        response_header.client_id = request_header.client_id;
        response_header.session_id = request_header.session_id;
        response_header.protocol_version = request_header.protocol_version;
        response_header.interface_version = request_header.interface_version;
        response_header.message_type = 0x80;  // Response
        response_header.return_code = 0x00;   // OK

        // 响应负载: accepted (1 byte) + reportId (8 bytes)
        uint8_t accepted = result.accepted ? 1 : 0;
        uint64_t report_id = htobe64(result.report_id);

        std::vector<uint8_t> response_payload;
        response_payload.push_back(accepted);
        for (int i = 0; i < 8; i++) {
            response_payload.push_back((report_id >> (i * 8)) & 0xFF);
        }

        response_header.length = htonl(sizeof(SomeIpHeader) + response_payload.size());

        // 发送响应
        write(client_fd, &response_header, sizeof(response_header));
        write(client_fd, response_payload.data(), response_payload.size());

        close(client_fd);
    }
};

SomeIpFotaProvider::SomeIpFotaProvider(std::shared_ptr<InventoryReporter> reporter)
    : reporter_(reporter)
    , pimpl_(std::make_unique<Impl>(this))
{
}

SomeIpFotaProvider::~SomeIpFotaProvider() = default;

bool SomeIpFotaProvider::start(const std::string& ip_address, uint16_t port) {
    return pimpl_->start(ip_address, port);
}

bool SomeIpFotaProvider::stop() {
    return pimpl_->stop();
}

bool SomeIpFotaProvider::isRunning() const {
    return pimpl_->isRunning();
}

AsyncReportResult SomeIpFotaProvider::handleRequest(const std::string& request_id, 
                                                    const std::string& reason) {
    return reporter_->reportInventoryAsync(request_id, reason);
}

} // namespace cgw_fota
```

- [ ] **步骤 3：创建 Provider 单元测试**

创建 `tests/test_someip_fota_provider.cpp`：

```cpp
#include "someip_fota_provider.h"
#include "inventory_reporter.h"
#include "someip_tbox_client.h"
#include "snapshot_assembler.h"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <thread>
#include <chrono>

using namespace cgw_fota;
using ::testing::_;
using ::testing::Return;
using ::testing::Invoke;

class MockSomeIpTboxClient : public SomeIpTboxClient {
public:
    MOCK_METHOD(bool, reportSoftwareInventory, (const VehicleSoftwareSnapshot& snapshot));
    MOCK_METHOD(bool, reportSoftwareInventoryWithRetry, (const VehicleSoftwareSnapshot& snapshot,
                                                       uint32_t max_retries,
                                                       uint32_t retry_interval_ms));
};

class MockSnapshotAssembler : public SnapshotAssembler {
public:
    MockSnapshotAssembler() : SnapshotAssembler(nullptr) {}
    MOCK_METHOD(bool, assembleSnapshot, (VehicleSoftwareSnapshot& snapshot));
};

class SomeIpFotaProviderTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto mock_tbox = std::make_shared<MockSomeIpTboxClient>();
        auto mock_assembler = std::make_shared<MockSnapshotAssembler>();

        EXPECT_CALL(*mock_assembler, assembleSnapshot(_))
            .WillRepeatedly(Invoke([](VehicleSoftwareSnapshot& s) {
                s.vin = "12345678901234567";
                s.snapshot_seq = 1;
                return true;
            }));

        EXPECT_CALL(*mock_tbox, reportSoftwareInventory(_))
            .WillRepeatedly(Return(true));

        reporter_ = std::make_shared<InventoryReporter>(mock_tbox, mock_assembler);
    }

    std::shared_ptr<InventoryReporter> reporter_;
};

TEST_F(SomeIpFotaProviderTest, StartAndStop) {
    SomeIpFotaProvider provider(reporter_);

    bool started = provider.start("127.0.0.1", 51120);
    EXPECT_TRUE(started);
    EXPECT_TRUE(provider.isRunning());

    // 等待一小段时间让服务器启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    bool stopped = provider.stop();
    EXPECT_TRUE(stopped);
    EXPECT_FALSE(provider.isRunning());
}

TEST_F(SomeIpFotaProviderTest, HandleRequestReturnsAccepted) {
    SomeIpFotaProvider provider(reporter_);

    // 直接测试 handleRequest 方法
    AsyncReportResult result = provider.handleRequest("test-001", "cloud_query");

    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);
}

TEST_F(SomeIpFotaProviderTest, ConcurrentRequests) {
    SomeIpFotaProvider provider(reporter_);

    // 快速连续发送两个请求
    auto result1 = provider.handleRequest("req-001", "cloud_query");
    
    // 等待一小段时间让异步任务启动
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    auto result2 = provider.handleRequest("req-002", "manual_retry");

    // 两个请求都应该被接受
    EXPECT_TRUE(result1.accepted);
    EXPECT_TRUE(result2.accepted);

    // 第二个请求应该合并到第一个
    EXPECT_EQ(result1.report_id, result2.report_id);
}
```

- [ ] **步骤 4：运行测试验证**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && make && ./tests/test_someip_fota_provider`
预期：所有测试通过

- [ ] **步骤 5：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add include/someip_fota_provider.h src/someip_fota_provider.cpp tests/test_someip_fota_provider.cpp
git commit -m "feat(cr-002): add SomeIpFotaProvider for handling incoming requests"
```

---

## 任务 6：集成 Provider 到 main.cpp

**文件：**
- 修改：`src/main.cpp`
- 测试：手动测试或集成测试

- [ ] **步骤 1：添加头文件引用**

在 `src/main.cpp` 顶部添加：

```cpp
#include "someip_fota_provider.h"
```

- [ ] **步骤 2：修改 main 函数启动流程**

修改 `src/main.cpp` 的 main 函数：

```cpp
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
              << config.getTboxIpAddress() << ":" << config.getTboxPort()
              << " (service_id=0x" << std::hex << config.getTboxServiceId() << std::dec << ")" << std::endl;

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
```

- [ ] **步骤 3：编译验证**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && cmake .. && make`
预期：编译成功

- [ ] **步骤 4：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add src/main.cpp
git commit -m "feat(cr-002): integrate SomeIpFotaProvider into main service"
```

---

## 任务 7：更新集成测试

**文件：**
- 修改：`tests/test_integration.cpp`

- [ ] **步骤 1：添加端到端主动请求测试**

在 `tests/test_integration.cpp` 添加测试：

```cpp
TEST_F(IntegrationTest, EndToEndActiveRequest) {
    // 模拟完整的主动请求上报流程
    
    // 1. 创建组件
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    
    // 2. 启动 Provider
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    bool started = provider->start("127.0.0.1", 51120);
    ASSERT_TRUE(started);
    
    // 3. 模拟 TBOX-TSP 发送请求
    AsyncReportResult result = provider->handleRequest("e2e-test-001", "cloud_query");
    
    // 4. 验证请求被接受
    EXPECT_TRUE(result.accepted);
    EXPECT_GT(result.report_id, 0);
    
    // 5. 等待异步任务完成
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    // 6. 停止 Provider
    provider->stop();
}

TEST_F(IntegrationTest, ConcurrentRequestMerging) {
    // 测试并发请求合并
    
    auto diag_client = std::make_shared<SomeIpFotaClient>();
    auto tbox_client = std::make_shared<SomeIpTboxClient>();
    auto assembler = std::make_shared<SnapshotAssembler>(diag_client);
    auto reporter = std::make_shared<InventoryReporter>(tbox_client, assembler);
    
    auto provider = std::make_shared<SomeIpFotaProvider>(reporter);
    provider->start("127.0.0.1", 51121);
    
    // 快速发送多个请求
    auto result1 = provider->handleRequest("concurrent-001", "cloud_query");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto result2 = provider->handleRequest("concurrent-002", "manual_retry");
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    auto result3 = provider->handleRequest("concurrent-003", "integration_test");
    
    // 所有请求都应该被接受
    EXPECT_TRUE(result1.accepted);
    EXPECT_TRUE(result2.accepted);
    EXPECT_TRUE(result3.accepted);
    
    // 后续请求应该合并到第一个
    EXPECT_EQ(result1.report_id, result2.report_id);
    EXPECT_EQ(result1.report_id, result3.report_id);
    
    provider->stop();
}
```

- [ ] **步骤 2：运行集成测试**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && make && ./tests/test_integration`
预期：所有测试通过

- [ ] **步骤 3：Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add tests/test_integration.cpp
git commit -m "test(cr-002): add end-to-end and concurrent request integration tests"
```

---

## 任务 8：最终验证

**文件：** 无（验证步骤）

- [ ] **步骤 1：运行所有测试**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota/build && cmake .. && make && ctest --output-on-failure`
预期：所有测试通过

- [ ] **步骤 2：更新 graphify 知识图谱**

运行：`cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota && graphify update .`
预期：图谱更新成功

- [ ] **步骤 3：最终 Commit**

```bash
cd /Users/hwyz_leo/Projects/open-iov/vehicle/cgw/iov-vehicle-cgw-fota
git add -A
git status  # 检查是否有遗漏的文件
git commit -m "feat(cr-002): complete CGW-FOTA-DSN-CR-002 implementation

- Add SomeIpFotaProvider (TCP 51120, Service ID 0x1120)
- Add METHOD_REQUEST_SOFTWARE_INVENTORY (0x0001)
- Enhance InventoryReporter with async and concurrent control
- Update TBOX service ID to 0x6101
- Update configuration and constants
- Add unit and integration tests"
```

---

## 规格覆盖度检查

| 规格章节 | 对应任务 | 状态 |
|---------|---------|------|
| 服务角色与寻址 | 任务 1, 2, 3 | ✅ |
| Method ID 分配 | 任务 1, 5 | ✅ |
| 架构设计 | 任务 4, 5 | ✅ |
| 配置变更 | 任务 2, 3 | ✅ |
| 错误处理 | 任务 4, 5 | ✅ |
| main.cpp 启动流程 | 任务 6 | ✅ |
| 测试策略 | 任务 4, 5, 7 | ✅ |

所有规格需求已覆盖。
