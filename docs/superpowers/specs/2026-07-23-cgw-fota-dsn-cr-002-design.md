# CGW-FOTA-DSN-CR-002 设计规格

## 1. 变更概述

**CR 编号**: CGW-FOTA-DSN-CR-002  
**变更日期**: 2026-07-23  
**变更类型**: 修改  
**影响实体**: 全局/Overview

### 变更摘要

为 CGW-FOTA 增补可部署的 SOME/IP Provider 接口，同时保留启动／激活后自动采集上报作为默认主路径。TBOX-TSP 可经 TBOX-SOMEIP client proxy 调用请求上报方法；CGW-FOTA 受理后异步调用 CGW-DIAG 获取版本清单，完成快照组装后再通过 TBOX-SOMEIP 提交上报。

### 关联文档

- 设计文档: CGW-FOTA-SPEC设计 (v1.1)
- 需求文档: CGW-FOTA-SPEC需求 (v1.1)
- 统一注册表: SOME/IP Service Registry
- 配对需求: CGW-FOTA-REQ-CR-002
- 前序: CGW-FOTA-DSN-CR-001

---

## 2. 服务角色与寻址

| 角色 | Service ID | Instance ID | 协议／端口 | 说明 |
|------|-----------|-------------|-----------|------|
| CGW-FOTA Provider | 0x1120 | 0x0001 | TCP 51120 | 接受 TBOX-TSP 的主动请求上报 |
| CGW-FOTA → DIAG Client | 0x1110 | 0x0001 | TCP 51110 | 调用 collectVehicleInventory() |
| CGW-FOTA → TBOX Client | 0x6101 | 0x0001 | TCP 56101 | 提交最终软件版本快照 |

所有服务寻址以整车 SOME/IP Service Registry 为唯一 SSOT。

---

## 3. Method ID 分配

- Method ID 为 service-scoped；`0x0001–0x7FFF` 用于 method，`0x8000–0xFFFF` 预留 event/notification
- 一经分配不复用、不重编号

| Method ID | 标准名 | 方法签名 | 方向 | 说明 |
|-----------|-------|---------|------|------|
| 0x0001 | METHOD_REQUEST_SOFTWARE_INVENTORY | requestSoftwareInventory(requestId, reason) → { accepted, reportId } | TBOX-TSP → CGW-FOTA | 异步请求采集并上报整车软件版本；立即返回受理结果 |

- 完整 Message ID 为 `0x1120_0001`
- `reason` 枚举值: `cloud_query`、`manual_retry`、`integration_test`
- 启动自动上报由内部触发器执行，不需要调用该 Method

---

## 4. 架构设计

### 4.1 整体架构

```
┌─────────────────────────────────────────────────────────────┐
│                      CGW-FOTA Service                       │
│                                                             │
│  ┌──────────────────┐         ┌──────────────────────────┐  │
│  │ SomeIpFotaProvider│         │   InventoryReporter      │  │
│  │ (新增 - TCP 51120)│────────>│   (增强并发控制)          │  │
│  │ 0x1120/0x0001     │         │                          │  │
│  └──────────────────┘         └──────────────────────────┘  │
│            │                           │                    │
│            │ 接收请求                   │ 触发采集上报        │
│            ▼                           ▼                    │
│  ┌──────────────────┐         ┌──────────────────────────┐  │
│  │ TBOX-TSP Client  │         │ SnapshotAssembler        │  │
│  │ (外部调用方)      │         │ SomeIpFotaClient (DIAG)  │  │
│  └──────────────────┘         │ SomeIpTboxClient (TBOX)  │  │
│                               └──────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

### 4.2 新增组件：SomeIpFotaProvider

**职责**: 监听 TBOX-TSP 的入站请求，处理 METHOD_REQUEST_SOFTWARE_INVENTORY。

**类设计**:
```cpp
class SomeIpFotaProvider {
public:
    SomeIpFotaProvider(std::shared_ptr<InventoryReporter> reporter);
    ~SomeIpFotaProvider();

    bool start(const std::string& ip_address, uint16_t port);
    bool stop();
    bool isRunning() const;

private:
    class Impl;
    std::unique_ptr<Impl> pimpl_;
    std::shared_ptr<InventoryReporter> reporter_;

    struct RequestResult {
        bool accepted;
        uint64_t report_id;
    };

    RequestResult handleRequest(const std::string& request_id, const std::string& reason);
};
```

**PImpl 实现** (类似现有 SomeIpFotaClient/SomeIpTboxClient):
- TCP Server 监听指定端口
- 解析 SOME/IP 消息头
- 路由 Method ID 0x0001 到 handleRequest
- 序列化响应并返回

### 4.3 修改组件：InventoryReporter

**新增功能**:
- 独立的 `report_seq_` 计数器 (std::atomic<uint64_t>)
- `is_collecting_` 标记 (std::atomic<bool>)
- `current_report_id_` 存储在途任务 ID

**新增方法**:
```cpp
struct AsyncReportResult {
    bool accepted;
    uint64_t report_id;
};

AsyncReportResult reportInventoryAsync(const std::string& request_id, const std::string& reason);
bool isCollecting() const;
uint64_t getCurrentReportId() const;
```

**并发控制逻辑**:
```
if (is_collecting_) {
    // 合并到在途任务
    return {accepted: true, report_id: current_report_id_};
}
is_collecting_ = true;
current_report_id_ = ++report_seq_;
// 异步启动采集上报
// 完成后 is_collecting_ = false
```

### 4.4 数据流

#### 启动自动上报（默认路径，不变）
1. CGW-FOTA 启动／激活并等待 CGW-DIAG、TBOX-SOMEIP 就绪
2. 调用 DIAG `collectVehicleInventory()` 获取权威快照
3. 追加 `snapshotSeq`／`reportId` 并执行节流、去重
4. 调用 TBOX-SOMEIP `reportSoftwareInventory(snapshot)`，由 TBOX-TSP 经 MQTT 上云

#### 主动请求上报（新增）
1. 云端查询 → MQTT → TBOX-TSP
2. TBOX-TSP 经 TBOX-SOMEIP client proxy 调用 `METHOD_REQUEST_SOFTWARE_INVENTORY`
3. CGW-FOTA 校验 `requestId`、执行并发合并／去重，返回 `{ accepted, reportId }`
4. 后台执行与自动上报相同的采集、组装和上报流程

---

## 5. 配置变更

### 5.1 constants.h

```cpp
// 新增 FOTA Provider 服务
constexpr uint16_t FOTA_PROVIDER_SERVICE_ID = 0x1120;
constexpr uint16_t FOTA_PROVIDER_INSTANCE_ID = 0x0001;
constexpr uint16_t FOTA_PROVIDER_PORT = 51120;

// 新增 Method ID (FOTA Provider 专用)
constexpr uint16_t METHOD_REQUEST_SOFTWARE_INVENTORY = 0x0001;

// 更新 TBOX 服务 ID (根据 CR-002)
constexpr uint16_t DEFAULT_TBOX_SERVICE_ID = 0x6101;
constexpr uint16_t DEFAULT_TBOX_PORT = 56101;
```

### 5.2 fota_config.yaml

```yaml
someip:
  # 新增 FOTA Provider 服务
  fota_provider:
    service_id: 0x1120
    instance_id: 0x0001
    ip_address: "0.0.0.0"
    port: 51120
  
  # 更新 TBOX 服务
  tbox_service:
    service_id: 0x6101  # 原 0x0002
    instance_id: 0x0001
    ip_address: "127.0.0.1"
    port: 56101  # 原 30502
```

---

## 6. 错误处理

复用现有错误码：
- CGW-FOTA-1003: 清单组装失败
- CGW-FOTA-1004: 车内发送失败
- CGW-FOTA-1005: TBOX 不可达
- CGW-FOTA-1006: 上报超时

新增场景处理：
- Provider 启动失败: 记录错误日志，服务退出
- 并发请求合并: 返回相同 reportId，日志记录合并事件
- requestId 重复: 执行去重，返回已有 reportId

---

## 7. main.cpp 启动流程

```
1. 加载配置 (ConfigLoader)
2. 创建 Client
   - SomeIpFotaClient (DIAG)
   - SomeIpTboxClient (TBOX)
3. 创建 SnapshotAssembler
4. 创建 InventoryReporter (增加 report_seq)
5. 创建 SomeIpFotaProvider (绑定 InventoryReporter)
6. 启动 Provider 监听 (TCP 51120)
7. 执行启动自动上报 (默认路径)
8. 主循环等待事件
```

---

## 8. 测试策略

### 8.1 单元测试

- `test_someip_fota_provider.cpp`
  - 测试 Provider 启动/停止
  - 测试请求处理逻辑
  - 测试并发合并行为

- `test_inventory_reporter.cpp` (增强)
  - 测试 reportInventoryAsync
  - 测试并发控制
  - 测试 reportId 生成

### 8.2 集成测试

- `test_integration.cpp` (增强)
  - 端到端主动请求上报流程
  - 并发请求场景
  - 错误恢复场景

---

## 9. 影响与边界

### 新增
- SomeIpFotaProvider 类及其实现
- InventoryReporter 并发控制机制
- 配置文件更新

### 不变
- 自动上报流程（默认路径）
- DIAG 快照模型
- TBOX MQTT topic
- 升级包下发、刷写、回滚等能力（不在本期范围）

---

## 10. 变更历史

- **v1.0** (2026-07-23): 初始设计，基于 CGW-FOTA-DSN-CR-002
