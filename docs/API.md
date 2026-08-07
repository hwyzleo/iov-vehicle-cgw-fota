# CGW-FOTA API Reference

## SnapshotAssembler

### Constructor

```cpp
SnapshotAssembler(std::shared_ptr<SomeIpFotaClient> client);
```

**Parameters**:
- `client`: Shared pointer to SOME/IP client for CGW-DIAG communication

### assembleSnapshot

```cpp
bool assembleSnapshot(const std::string& vin, VehicleSoftwareSnapshot& snapshot);
```

Assembles a vehicle software snapshot.

**Parameters**:
- `vin`: Vehicle Identification Number
- `snapshot`: Output parameter containing the assembled snapshot

**Returns**: `true` if successful, `false` otherwise

**Thread Safety**: This method is thread-safe

### setThrottleInterval

```cpp
void setThrottleInterval(uint32_t interval_ms);
```

Sets the minimum interval between reports.

**Parameters**:
- `interval_ms`: Interval in milliseconds

### setMaxEcuCount

```cpp
void setMaxEcuCount(uint32_t max_count);
```

Sets the maximum number of ECUs in a snapshot.

**Parameters**:
- `max_count`: Maximum ECU count

## InventoryReporter

### Constructor

```cpp
InventoryReporter(std::shared_ptr<SomeIpTboxClient> tbox_client,
                 std::shared_ptr<SnapshotAssembler> assembler);
```

**Parameters**:
- `tbox_client`: Shared pointer to SOME/IP client for TBOX communication
- `assembler`: Shared pointer to snapshot assembler

### reportInventory

```cpp
bool reportInventory(const std::string& vin);
```

Reports vehicle inventory to TBOX.

**Parameters**:
- `vin`: Vehicle Identification Number

**Returns**: `true` if successful, `false` otherwise

**Thread Safety**: This method is thread-safe

### setRetryPolicy

```cpp
void setRetryPolicy(uint32_t max_retries, uint32_t retry_interval_ms);
```

Sets the retry policy for TBOX communication.

**Parameters**:
- `max_retries`: Maximum number of retries
- `retry_interval_ms`: Interval between retries in milliseconds

### setDedupWindowSize

```cpp
void setDedupWindowSize(uint32_t window_size);
```

Sets the deduplication window size.

**Parameters**:
- `window_size`: Number of recent sequence numbers to remember

## SomeIpFotaClient

### Constructor

```cpp
SomeIpFotaClient();
```

### connect

```cpp
bool connect(const std::string& ip_address, uint16_t port);
```

Connects to CGW-DIAG service.

**Parameters**:
- `ip_address`: IP address of CGW-DIAG service
- `port`: Port number

**Returns**: `true` if connected successfully, `false` otherwise

### disconnect

```cpp
bool disconnect();
```

Disconnects from CGW-DIAG service.

**Returns**: `true` if disconnected successfully, `false` otherwise

### isConnected

```cpp
bool isConnected() const;
```

Checks if connected to CGW-DIAG service.

**Returns**: `true` if connected, `false` otherwise

### collectVehicleInventory

```cpp
bool collectVehicleInventory(const std::string& vin, VehicleSoftwareSnapshot& snapshot);
```

Collects vehicle inventory from CGW-DIAG.

**Parameters**:
- `vin`: Vehicle Identification Number
- `snapshot`: Output parameter containing the collected inventory

**Returns**: `true` if successful, `false` otherwise

### getEcuVersion

```cpp
bool getEcuVersion(const std::string& ecu_id, EcuVersionEntry& entry);
```

Gets version information for a specific ECU.

**Parameters**:
- `ecu_id`: ECU identifier
- `entry`: Output parameter containing the version information

**Returns**: `true` if successful, `false` otherwise

### getRegistryVersion

```cpp
bool getRegistryVersion(std::string& version);
```

Gets the registry version.

**Parameters**:
- `version`: Output parameter containing the registry version

**Returns**: `true` if successful, `false` otherwise

## SomeIpTboxClient

### Constructor

```cpp
SomeIpTboxClient();
```

### connect

```cpp
bool connect(const std::string& ip_address, uint16_t port);
```

Connects to TBOX service.

**Parameters**:
- `ip_address`: IP address of TBOX service
- `port`: Port number

**Returns**: `true` if connected successfully, `false` otherwise

### disconnect

```cpp
bool disconnect();
```

Disconnects from TBOX service.

**Returns**: `true` if disconnected successfully, `false` otherwise

### isConnected

```cpp
bool isConnected() const;
```

Checks if connected to TBOX service.

**Returns**: `true` if connected, `false` otherwise

### reportSoftwareInventory

```cpp
bool reportSoftwareInventory(const VehicleSoftwareSnapshot& snapshot);
```

Reports software inventory to TBOX.

**Parameters**:
- `snapshot`: The software inventory to report

**Returns**: `true` if successful, `false` otherwise

### reportSoftwareInventoryWithRetry

```cpp
bool reportSoftwareInventoryWithRetry(const VehicleSoftwareSnapshot& snapshot,
                                     uint32_t max_retries,
                                     uint32_t retry_interval_ms);
```

Reports software inventory to TBOX with retry mechanism.

**Parameters**:
- `snapshot`: The software inventory to report
- `max_retries`: Maximum number of retries
- `retry_interval_ms`: Interval between retries in milliseconds

**Returns**: `true` if successful, `false` otherwise

## FotaConfig

CGW-FOTA 通过 cgw-framework-config 获取不可变配置快照，再经 `FotaConfig::from()` 映射为只读业务配置（CGW-FOTA-DSN-CR-004）。业务代码不直接接触 yaml-cpp。

Header: `include/cgw/fota/config/fota_config.hpp`

### from

```cpp
static FotaConfig from(const cgw::fw::config::ConfigSnapshot& snapshot);
```

从不可变快照构建已校验的 `FotaConfig`。任一缺省值以外的不合法类型、越界值、未知字段或跨字段冲突抛 `FotaConfigException`（fail-closed）。

### logConfigFrom

```cpp
static cgw::fw::log::LogConfig
logConfigFrom(const cgw::fw::config::ConfigSnapshot& snapshot);
```

从同一快照读取 `common.log.*` 与 `fota.log.*`，构建 Logger 配置。

### FotaConfig 字段

- `autoReportOnStart`：启动后是否自动上报（默认 true）
- `changeDetectionEnabled`：是否启用变更检测（默认 true）
- `minReportInterval`：自动上报节流最小间隔（默认 300000ms）
- `maxPendingRequests`：等待队列上限 1..1024（默认 32）
- `diagCollectTimeout`：DIAG 采集超时（默认 30000ms）
- `diagRetry`：采集重试 `{maxAttempts, backoff}`（默认 2 / 1000ms）
- `tboxSubmitTimeout`：TBOX 提交超时（默认 10000ms）
- `tboxRetry`：提交重试 `{maxAttempts, backoff}`（默认 3 / 1000ms）

> SOME/IP 寻址（Service/Instance/Method ID、协议、端口）不进入 fota.yaml，继续以整车 SOME/IP Service Registry 为唯一 SSOT；过渡期由 `constants.h` 供给。
