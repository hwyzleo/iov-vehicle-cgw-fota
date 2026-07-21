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

## ConfigLoader

### Constructor

```cpp
ConfigLoader();
```

### loadConfig

```cpp
bool loadConfig(const std::string& config_path);
```

Loads configuration from a YAML file.

**Parameters**:
- `config_path`: Path to the configuration file

**Returns**: `true` if loaded successfully, `false` otherwise

### Getter Methods

The class provides getter methods for all configuration parameters:

- `getMaxEcuCount()`: Returns maximum ECU count
- `getSnapshotSeqInitial()`: Returns initial snapshot sequence number
- `getThrottleIntervalMs()`: Returns throttle interval in milliseconds
- `getDedupWindowSize()`: Returns deduplication window size
- `getDiagServiceId()`: Returns CGW-DIAG service ID
- `getDiagInstanceId()`: Returns CGW-DIAG instance ID
- `getDiagIpAddress()`: Returns CGW-DIAG IP address
- `getDiagPort()`: Returns CGW-DIAG port
- `getTboxServiceId()`: Returns TBOX service ID
- `getTboxInstanceId()`: Returns TBOX instance ID
- `getTboxIpAddress()`: Returns TBOX IP address
- `getTboxPort()`: Returns TBOX port
- `getInitialReportDelayMs()`: Returns initial report delay in milliseconds
- `getMaxRetryCount()`: Returns maximum retry count
- `getRetryIntervalMs()`: Returns retry interval in milliseconds
- `getLogLevel()`: Returns log level
- `getLogFile()`: Returns log file path
