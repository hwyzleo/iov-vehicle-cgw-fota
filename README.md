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

### Quick Build (Recommended)

Use the provided build script for a streamlined build process:

```bash
./scripts/build.sh
```

The build script supports the following options:
- `--clean`: Clean build directory before building
- `--no-test`: Skip running tests after build
- `--help`: Show help information

Examples:
```bash
./scripts/build.sh                  # Full build with tests
./scripts/build.sh --no-test        # Build only, skip tests
./scripts/build.sh --clean          # Clean and rebuild
```

> CGW-FOTA-DSN-CR-008: 正式 install/部署由 CGW-BUILD release-set 编排，
> 开发脚本不再提供 `--install` 旁路。本地 DESTDIR staging 验证：
> `DESTDIR=/tmp/fota-root cmake --install build --prefix /usr --component cgw-fota-runtime`

### Manual Build Instructions

If you prefer manual builds:

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

CGW-FOTA 通过 **cgw-framework-config** 获取不可变配置快照（CGW-FOTA-DSN-CR-004）。启动时执行 `Config::load("fota", options)` 六阶段解析与 ordered-overlay，再经 `FotaConfig::from(snapshot)` 完成类型/范围/跨字段校验；任一失败均 fail-closed 终止。业务代码不直接接触 yaml-cpp。

- 量产 `configRoots` 至少包含 `/etc/cgw`；正式 `/etc/cgw/conf.d/fota.yaml` 由 CGW-BUILD 从 `config/fota.default.yaml` 生成。
- `config/fota.yaml`、`config/common.yaml` 仅开发夹具，不进入正式 install component。
- SOME/IP 寻址（Service/Instance/Method ID、协议、端口）不进入 fota.yaml，继续以整车 SOME/IP Service Registry 为唯一 SSOT。

fota.* 契约（`config/fota.default.yaml`）：

```yaml
fota:
  inventory:
    auto_report_on_start: true
    change_detection_enabled: true
    min_report_interval_ms: 300000
    max_pending_requests: 32
  diag:
    collect_timeout_ms: 30000
    retry_max_attempts: 2
    retry_backoff_ms: 1000
  tbox:
    submit_timeout_ms: 10000
    retry_max_attempts: 3
    retry_backoff_ms: 1000
  log: {}
```

## Usage

### Running the Service

```bash
# 量产：configRoots 默认 /etc/cgw
./cgw-fota

# 开发/测试：指定含 common.yaml 的 config 根
./cgw-fota config
```

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
