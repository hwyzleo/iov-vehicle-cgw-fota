# CGW-FOTA Service

CGW-FOTA (Gateway Firmware Over-The-Air) service for vehicle software version snapshot collection and reporting.

## Overview

CGW-FOTA is responsible for:
- Collecting vehicle software version snapshots from CGW-DIAG
- Assembling structured software inventory reports
- Reporting inventory to TBOX via SOME/IP
- Supporting cloud synchronization via TBOX-MQTT

## Architecture

The service consists of two main components:
1. **SnapshotAssembler**: Consumes version list from CGW-DIAG and assembles vehicle snapshot
2. **InventoryReporter**: Sends snapshot to TBOX via SOME/IP

## Building

### Prerequisites

- C++17 compiler
- CMake 3.10+
- yaml-cpp
- nlohmann_json
- GTest (for testing)

### Build Instructions

```bash
mkdir build
cd build
cmake ..
make
```

### Running Tests

```bash
cd build
./CgwFotaTests
```

## Configuration

Configuration is loaded from `config/fota_config.yaml`:

```yaml
fota:
  snapshot:
    max_ecu_count: 100
    throttle_interval_ms: 5000
  someip:
    diag_service:
      service_id: 0x0001
      ip_address: "127.0.0.1"
      port: 30501
    tbox_service:
      service_id: 0x0002
      ip_address: "127.0.0.1"
      port: 30502
```

## Usage

### Running the Service

```bash
./cgw_fota [config_path]
```

If no config path is provided, the service will use `config/fota_config.yaml`.

### Stopping the Service

Press `Ctrl+C` to stop the service gracefully.

## API Reference

### SnapshotAssembler

- `assembleSnapshot(vin, snapshot)`: Assembles a vehicle software snapshot
- `setThrottleInterval(interval_ms)`: Sets reporting throttle interval
- `setMaxEcuCount(max_count)`: Sets maximum ECU count

### InventoryReporter

- `reportInventory(vin)`: Reports vehicle inventory to TBOX
- `setRetryPolicy(max_retries, retry_interval_ms)`: Sets retry policy
- `setDedupWindowSize(window_size)`: Sets deduplication window size

## Error Codes

- CGW-FOTA-1003: 清单组装失败
- CGW-FOTA-1004: 车内发送失败
- CGW-FOTA-1005: TBOX 不可达
- CGW-FOTA-1006: 上报超时

## Dependencies

- CGW-DIAG: Version collection interface (`collectVehicleInventory()`)
- TBOX-MQTT: Uplink channel with `fota` business topic routing
- VMD: Provides VIN

## License

See LICENSE file.
