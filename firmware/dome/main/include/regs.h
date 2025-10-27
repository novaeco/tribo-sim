#pragma once
// Register map for I2C slave (Dome)
#define DOME_REG_STATUS       0x00
#define DOME_REG_MODE         0x01
#define DOME_REG_CCT1_L       0x02  // CH1 Day permille (LE)
#define DOME_REG_CCT1_H       0x03
#define DOME_REG_CCT2_L       0x04  // CH2 Warm permille (LE)
#define DOME_REG_CCT2_H       0x05
#define DOME_REG_UVA_SET      0x06  // 0..10000 permille
#define DOME_REG_UVB_SET      0x07  // 0..10000 permille
#define DOME_REG_SKY_CFG      0x08  // 0=off,1=blue,2=twinkle
#define DOME_REG_UVA_CLAMP    0x09  // max permille (UVA)
#define DOME_REG_UVB_CLAMP    0x0A  // max permille (UVB)
#define DOME_REG_UVB_PERIOD_S 0x0B  // UVB pulse period (s)
#define DOME_REG_UVB_DUTY_PM  0x0C  // UVB pulse duty permille
#define DOME_REG_TLM_T_HEAT   0x20  // int Celsius
#define DOME_REG_TLM_FLAGS    0x21  // mirror status or extra flags

// STATUS bits
#define ST_OT         (1<<0)  // Over-temp soft
#define ST_UVA_LIMIT  (1<<1)
#define ST_UVB_LIMIT  (1<<2)
#define ST_FAN_FAIL   (1<<3)
#define ST_BUS_LOSS   (1<<4)
#define ST_INTERLOCK  (1<<5)
#define ST_THERM_HARD (1<<6)  // if readback wired
// MODE bits
#define MODE_ON      (1<<0)
#define MODE_SKY     (1<<1)
#define MODE_LOCK    (1<<7)
