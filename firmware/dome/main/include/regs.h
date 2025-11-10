#pragma once

// Register map for I2C slave (Dome)
// Multi-byte blocks are contiguous to support burst transactions.
#define DOME_REG_STATUS               0x00
#define DOME_REG_MODE                 0x01

#define DOME_REG_BLOCK_CCT            0x02
#define DOME_REG_BLOCK_CCT_LEN        4
#define DOME_REG_CCT_DAY_L            (DOME_REG_BLOCK_CCT + 0)
#define DOME_REG_CCT_DAY_H            (DOME_REG_BLOCK_CCT + 1)
#define DOME_REG_CCT_WARM_L           (DOME_REG_BLOCK_CCT + 2)
#define DOME_REG_CCT_WARM_H           (DOME_REG_BLOCK_CCT + 3)

#define DOME_REG_BLOCK_UVA            0x06
#define DOME_REG_BLOCK_UVA_LEN        4
#define DOME_REG_UVA_SET_L            (DOME_REG_BLOCK_UVA + 0)
#define DOME_REG_UVA_SET_H            (DOME_REG_BLOCK_UVA + 1)
#define DOME_REG_UVA_CLAMP_L          (DOME_REG_BLOCK_UVA + 2)
#define DOME_REG_UVA_CLAMP_H          (DOME_REG_BLOCK_UVA + 3)

#define DOME_REG_BLOCK_UVB            0x0A
#define DOME_REG_BLOCK_UVB_LEN        3
#define DOME_REG_UVB_PERIOD_S         (DOME_REG_BLOCK_UVB + 0)
#define DOME_REG_UVB_DUTY_PM          (DOME_REG_BLOCK_UVB + 1)
#define DOME_REG_UVB_CLAMP_PM         (DOME_REG_BLOCK_UVB + 2)

#define DOME_REG_SKY_CFG              0x0D

#define DOME_REG_BLOCK_FAN            0x0E
#define DOME_REG_BLOCK_FAN_LEN        3
#define DOME_REG_FAN_FLAGS            (DOME_REG_BLOCK_FAN + 0)
#define DOME_REG_FAN_PWM_L            (DOME_REG_BLOCK_FAN + 1)
#define DOME_REG_FAN_PWM_H            (DOME_REG_BLOCK_FAN + 2)

#define DOME_REG_BLOCK_UVI            0x12
#define DOME_REG_BLOCK_UVI_LEN        4
#define DOME_REG_UVI_IRR_L            (DOME_REG_BLOCK_UVI + 0)
#define DOME_REG_UVI_IRR_H            (DOME_REG_BLOCK_UVI + 1)
#define DOME_REG_UVI_INDEX_L          (DOME_REG_BLOCK_UVI + 2)
#define DOME_REG_UVI_INDEX_H          (DOME_REG_BLOCK_UVI + 3)

#define DOME_REG_BLOCK_HEATSINK       0x20
#define DOME_REG_BLOCK_HEATSINK_LEN   2
#define DOME_REG_TLM_T_HEAT           (DOME_REG_BLOCK_HEATSINK + 0)
#define DOME_REG_TLM_FLAGS            (DOME_REG_BLOCK_HEATSINK + 1)

#define DOME_REG_BLOCK_DIAG           0x24
#define DOME_REG_BLOCK_DIAG_LEN       28
#define DOME_REG_DIAG_I2C_ERR_L       (DOME_REG_BLOCK_DIAG + 0)
#define DOME_REG_DIAG_I2C_ERR_H       (DOME_REG_BLOCK_DIAG + 1)
#define DOME_REG_DIAG_PWM_ERR_L       (DOME_REG_BLOCK_DIAG + 2)
#define DOME_REG_DIAG_PWM_ERR_H       (DOME_REG_BLOCK_DIAG + 3)
#define DOME_REG_DIAG_INT_COUNT_L     (DOME_REG_BLOCK_DIAG + 4)
#define DOME_REG_DIAG_INT_COUNT_H     (DOME_REG_BLOCK_DIAG + 5)
#define DOME_REG_DIAG_UV_EVENT_COUNT  (DOME_REG_BLOCK_DIAG + 6)
#define DOME_REG_DIAG_UV_EVENT_HEAD   (DOME_REG_BLOCK_DIAG + 7)
#define DOME_REG_DIAG_CMD             (DOME_REG_BLOCK_DIAG + 8)
#define DOME_REG_DIAG_UV_HISTORY      (DOME_REG_BLOCK_DIAG + 9)
#define DOME_REG_DIAG_UV_HISTORY_LEN  16

#define DOME_DIAG_UV_HISTORY_DEPTH    4
#define DOME_DIAG_UV_EVENT_STRIDE     4
#define DOME_DIAG_CMD_NONE            0x00u
#define DOME_DIAG_CMD_RESET           0xA5u
#define DOME_DIAG_UV_EVENT_TIMESTAMP_MASK 0x3FFFFFFFu
#define DOME_DIAG_UV_EVENT_CH_UVA          0x40000000u
#define DOME_DIAG_UV_EVENT_CH_UVB          0x80000000u
#define DOME_DIAG_UV_EVENT_CHANNEL_MASK    (DOME_DIAG_UV_EVENT_CH_UVA | DOME_DIAG_UV_EVENT_CH_UVB)

#define DOME_REG_BLOCK_OTA_CTRL       0x40
#define DOME_REG_BLOCK_OTA_CTRL_LEN   4
#define DOME_REG_OTA_CMD              (DOME_REG_BLOCK_OTA_CTRL + 0)
#define DOME_REG_OTA_STATUS           (DOME_REG_BLOCK_OTA_CTRL + 1)
#define DOME_REG_OTA_ERROR            (DOME_REG_BLOCK_OTA_CTRL + 2)
#define DOME_REG_OTA_RESERVED         (DOME_REG_BLOCK_OTA_CTRL + 3)

#define DOME_REG_BLOCK_OTA_DATA       0x44
#define DOME_REG_BLOCK_OTA_META       0x60
#define DOME_REG_BLOCK_OTA_META_LEN   104
#define DOME_REG_OTA_EXPECTED_SIZE_L  (DOME_REG_BLOCK_OTA_META + 0)
#define DOME_REG_OTA_EXPECTED_SHA     (DOME_REG_BLOCK_OTA_META + 4)
#define DOME_REG_OTA_VERSION          (DOME_REG_BLOCK_OTA_META + 36)
#define DOME_REG_OTA_VERSION_LEN      32
#define DOME_REG_OTA_FLAGS            (DOME_REG_BLOCK_OTA_META + 68)
#define DOME_REG_OTA_STATUS_MSG       (DOME_REG_BLOCK_OTA_META + 72)
#define DOME_REG_OTA_STATUS_MSG_LEN   32


#define DOME_REG_BLOCK_OTA_DATA_LEN   32

// STATUS bits
#define ST_OT         (1<<0)  // Over-temp soft
#define ST_UVA_LIMIT  (1<<1)
#define ST_UVB_LIMIT  (1<<2)
#define ST_FAN_FAIL   (1<<3)
#define ST_BUS_LOSS   (1<<4)
#define ST_INTERLOCK  (1<<5)
#define ST_THERM_HARD (1<<6)  // hardware thermostat asserted
#define ST_UVI_FAULT  (1<<7)

// MODE bits
#define MODE_ON      (1<<0)
#define MODE_SKY     (1<<1)
#define MODE_LOCK    (1<<7)

// FAN flags
#define FAN_FLAG_PRESENT   (1<<0)
#define FAN_FLAG_RUNNING   (1<<1)
#define FAN_FLAG_ALARM     (1<<2)

// OTA commands / status codes
#define DOME_OTA_CMD_IDLE      0x00
#define DOME_OTA_CMD_BEGIN     0x01
#define DOME_OTA_CMD_WRITE     0x02
#define DOME_OTA_CMD_COMMIT    0x03
#define DOME_OTA_CMD_ABORT     0x04

#define DOME_OTA_STATUS_IDLE   0x00
#define DOME_OTA_STATUS_BUSY   0x01
#define DOME_OTA_STATUS_DONE   0x02
#define DOME_OTA_STATUS_ERROR  0xFF

#define DOME_OTA_FLAG_META_READY  0x01
#define DOME_OTA_FLAG_HASH_OK     0x02
#define DOME_OTA_FLAG_HASH_FAIL   0x04
#define DOME_OTA_FLAG_ROLLBACK   0x08
#define DOME_OTA_FLAG_APPLIED    0x10

