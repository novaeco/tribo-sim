# Security Notes - ESP32-P4 7-inch Panel

## Production Security Configuration

### 1. ✅ Task Watchdog (ACTIVE)

Protects against system hangs and infinite loops.

**Configuration in `sdkconfig.defaults`:**
```
CONFIG_ESP_TASK_WDT_EN=y
CONFIG_ESP_TASK_WDT_TIMEOUT_S=30
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
```

**Timeout:** 30 seconds (generous for WiFi+BLE+LVGL operations)

If a task blocks for more than 30 seconds, the watchdog will reset the device.

### 2. ⏸️ NVS Encryption (NOT ACTIVE - Requires Manual Configuration)

NVS encryption requires eFuse configuration on ESP32-P4 which is a one-time irreversible operation.

**To enable NVS Encryption:**

1. Configure the eFuse key ID in menuconfig:
   ```bash
   idf.py menuconfig
   # Navigate to: Component config → NVS → NVS Encryption
   # Set CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID to a valid block (1-5)
   ```

2. Generate and burn the HMAC key:
   ```bash
   espefuse.py -p COMX burn_key BLOCK_KEYN esp32p4_nvs_key --keypurpose HMAC
   ```

3. Enable in sdkconfig.defaults:
   ```
   CONFIG_NVS_ENCRYPTION=y
   CONFIG_NVS_SEC_HMAC_EFUSE_KEY_ID=1
   ```

⚠️ **WARNING:** Burning eFuse keys is PERMANENT and cannot be undone!

### 3. 🔒 Flash Encryption & Secure Boot (NOT ACTIVE - PRODUCTION ONLY)

These are for final production firmware only!

⚠️ **WARNING:** Once enabled in RELEASE mode, the device is permanently locked!

```
# CONFIG_SECURE_FLASH_ENC_ENABLED=y
# CONFIG_SECURE_FLASH_ENCRYPTION_MODE_DEVELOPMENT=y
# CONFIG_SECURE_BOOT=y
```

**To enable for production:**
1. Generate signing key: `espsecure.py generate_signing_key secure_boot_signing_key.pem`
2. Enable configs above
3. Flash once - device will burn eFuses and lock

## WiFi Credentials Storage

- Credentials are saved to NVS after successful connection
- Currently stored in plain text (NVS encryption not active)
- Default SSID/password are empty strings (no hardcoded secrets)
- UI prompts user to configure WiFi on first boot

## Current Security Status

| Feature | Status | Notes |
|---------|--------|-------|
| Task Watchdog | ✅ Active | 30s timeout |
| NVS Encryption | ⏸️ Pending | Requires eFuse burn |
| Flash Encryption | ❌ Off | Production only |
| Secure Boot | ❌ Off | Production only |
| WiFi Hardcoded Secrets | ✅ Removed | Empty defaults |
| Code Warnings | ✅ Clean | No warnings in app code |

## Build & Flash Commands

```bash
# Full clean build
idf.py fullclean
idf.py set-target esp32p4
idf.py build

# Flash to device
idf.py -p COMX flash

# Monitor
idf.py -p COMX monitor

# Configure security options
idf.py menuconfig
```

## Hardware Security Notes

- **ESP32-P4:** Main MCU with Secure Boot V2 support
- **ESP32-C6 Co-processor:** Runs ESP-Hosted v2.8.5 firmware
- **SD Card:** Not encrypted (consider application-level encryption if needed)
- **Display:** No security concerns

## Revision History

- 2026-01-08: Initial security documentation
- 2026-01-08: Enabled Task Watchdog (30s timeout)
- 2026-01-08: Documented NVS Encryption requirements for ESP32-P4
