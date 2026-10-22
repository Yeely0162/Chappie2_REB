#include "I2C_BM8563.h"
#include "../ChappieI2C.hpp"

I2C_BM8563::I2C_BM8563(uint8_t deviceAddress, TwoWire &i2cPort) {
  _deviceAddress = deviceAddress;
  _i2cPort = &i2cPort;
}

void I2C_BM8563::begin(void) {
  ChappieI2CGuard lock;

  /* Only the two control registers are touched here: the first byte is the
   * register pointer, the two following bytes are written to 0x00 and 0x01.
   *
   *   0x00  Control_1  - clear STOP (start the oscillator), keep TEST off
   *   0x01  Control_2  - clear the alarm/timer interrupt enables
   *
   * The time registers (0x02 onwards) are deliberately left untouched: an
   * init path that writes them would reset the running clock at every boot. */
  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x00);
  _i2cPort->write(0x00);
  _i2cPort->write(0x00);
  if (_i2cPort->endTransmission() != 0) {
    printf("[RTC] begin write failed\n");
    return;
  }

  /* Read the control register back: bit 5 is STOP. A set bit means the
   * oscillator is halted and the time would look frozen to the UI. */
  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x00);
  if (_i2cPort->endTransmission(false) != 0) {
    printf("[RTC] begin verify: write failed\n");
    return;
  }
  if (_i2cPort->requestFrom(_deviceAddress, 1) != 1) {
    printf("[RTC] begin verify: read failed\n");
    return;
  }
  const uint8_t control1 = (uint8_t)_i2cPort->read();
  if (control1 & 0x20)
    printf("[RTC] begin: clock still stopped (control1=0x%02X)\n", (unsigned)control1);
}

bool I2C_BM8563::getVoltLow() {
  uint8_t data = ReadReg(0x02);
  return data & 0x80; // RTCC_VLSEC_MASK
}

uint8_t I2C_BM8563::bcd2ToByte(uint8_t value) {
  uint8_t tmp = 0;
  tmp = ((uint8_t)(value & (uint8_t)0xF0) >> (uint8_t)0x4) * 10;
  return (tmp + (value & (uint8_t)0x0F));
}

uint8_t I2C_BM8563::byteToBcd2(uint8_t value) {
  uint8_t bcdhigh = 0;

  while (value >= 10) {
    bcdhigh++;
    value -= 10;
  }

  return ((uint8_t)(bcdhigh << 4) | value);
}

bool I2C_BM8563::getTime(I2C_BM8563_TimeTypeDef* I2C_BM8563_TimeStruct) {
  if (I2C_BM8563_TimeStruct == NULL)
    return false;

  ChappieI2CGuard lock;
  uint8_t buf[3] = {0};

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x02);
  if (_i2cPort->endTransmission(false) != 0) {
    printf("[RTC] getTime write failed\n");
    return false;
  }
  if (_i2cPort->requestFrom(_deviceAddress, 3) != 3) {
    printf("[RTC] getTime read failed\n");
    return false;
  }
  if (_i2cPort->available() < 3) {
    printf("[RTC] getTime short read\n");
    return false;
  }
  buf[0] = _i2cPort->read();
  buf[1] = _i2cPort->read();
  buf[2] = _i2cPort->read();

  /* Mask only the BCD flag bits. The values are range-checked by the UI;
   * 00:00:00 from a never-set RTC is a valid reading. */
  I2C_BM8563_TimeStruct->seconds = bcd2ToByte(buf[0] & 0x7f);
  I2C_BM8563_TimeStruct->minutes = bcd2ToByte(buf[1] & 0x7f);
  I2C_BM8563_TimeStruct->hours   = bcd2ToByte(buf[2] & 0x3f);
  return true;
}

void I2C_BM8563::setTime(I2C_BM8563_TimeTypeDef* I2C_BM8563_TimeStruct) {
  if (I2C_BM8563_TimeStruct == NULL) {
    return;
  }

  ChappieI2CGuard lock;

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x02);
  _i2cPort->write(byteToBcd2(I2C_BM8563_TimeStruct->seconds));
  _i2cPort->write(byteToBcd2(I2C_BM8563_TimeStruct->minutes));
  _i2cPort->write(byteToBcd2(I2C_BM8563_TimeStruct->hours));
  if (_i2cPort->endTransmission() != 0)
    printf("[RTC] setTime failed\n");
}

bool I2C_BM8563::getDate(I2C_BM8563_DateTypeDef* I2C_BM8563_DateStruct) {
  if (I2C_BM8563_DateStruct == NULL)
    return false;

  ChappieI2CGuard lock;
  uint8_t buf[4] = {0};

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x05);
  if (_i2cPort->endTransmission(false) != 0) {
    printf("[RTC] getDate write failed\n");
    return false;
  }
  if (_i2cPort->requestFrom(_deviceAddress, 4) != 4) {
    printf("[RTC] getDate read failed\n");
    return false;
  }
  if (_i2cPort->available() < 4) {
    printf("[RTC] getDate short read\n");
    return false;
  }
  buf[0] = _i2cPort->read();
  buf[1] = _i2cPort->read();
  buf[2] = _i2cPort->read();
  buf[3] = _i2cPort->read();

  I2C_BM8563_DateStruct->date    = bcd2ToByte(buf[0] & 0x3f);
  I2C_BM8563_DateStruct->weekDay = bcd2ToByte(buf[1] & 0x07);
  I2C_BM8563_DateStruct->month   = bcd2ToByte(buf[2] & 0x1f);

  if (buf[2] & 0x80) {
    I2C_BM8563_DateStruct->year = 1900 + bcd2ToByte(buf[3] & 0xff);
  } else {
    I2C_BM8563_DateStruct->year = 2000 + bcd2ToByte(buf[3] & 0xff);
  }
  return true;
}

void I2C_BM8563::setDate(I2C_BM8563_DateTypeDef* I2C_BM8563_DateStruct) {
  if (I2C_BM8563_DateStruct == NULL) {
    return;
  }

  ChappieI2CGuard lock;

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(0x05);
  _i2cPort->write(byteToBcd2(I2C_BM8563_DateStruct->date));
  _i2cPort->write(byteToBcd2(I2C_BM8563_DateStruct->weekDay));

  if (I2C_BM8563_DateStruct->year < 2000) {
    _i2cPort->write(byteToBcd2(I2C_BM8563_DateStruct->month) | 0x80);
    _i2cPort->write(byteToBcd2((uint8_t)(I2C_BM8563_DateStruct->year % 100)));
  } else {
    _i2cPort->write(byteToBcd2(I2C_BM8563_DateStruct->month) | 0x00);
    _i2cPort->write(byteToBcd2((uint8_t)(I2C_BM8563_DateStruct->year % 100)));
  }

  if (_i2cPort->endTransmission() != 0)
    printf("[RTC] setDate failed\n");
}

void I2C_BM8563::WriteReg(uint8_t reg, uint8_t data) {
  ChappieI2CGuard lock;

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(reg);
  _i2cPort->write(data);
  if (_i2cPort->endTransmission() != 0)
    printf("[RTC] write 0x%02X failed\n", (unsigned)reg);
}

uint8_t I2C_BM8563::ReadReg(uint8_t reg) {
  ChappieI2CGuard lock;

  _i2cPort->beginTransmission(_deviceAddress);
  _i2cPort->write(reg);
  if (_i2cPort->endTransmission(false) != 0) {
    printf("[RTC] read 0x%02X failed: write\n", (unsigned)reg);
    return 0;
  }
  if (_i2cPort->requestFrom(_deviceAddress, 1) != 1) {
    printf("[RTC] read 0x%02X failed: read\n", (unsigned)reg);
    return 0;
  }
  int data = _i2cPort->read();
  return (data < 0) ? 0 : (uint8_t)data;
}

int I2C_BM8563::SetAlarmIRQ(int afterSeconds) {
  uint8_t reg_value = 0;
  reg_value = ReadReg(0x01);

  if (afterSeconds < 0) {
    reg_value &= ~(1 << 0);
    WriteReg(0x01, reg_value);
    reg_value = 0x03;
    WriteReg(0x0E, reg_value);
    return -1;
  }

  uint8_t type_value = 2;
  uint8_t div = 1;
  if (afterSeconds > 255) {
    div = 60;
    type_value = 0x83;
  } else {
    type_value = 0x82;
  }

  afterSeconds = (afterSeconds / div) & 0xFF;
  WriteReg(0x0F, afterSeconds);
  WriteReg(0x0E, type_value);

  reg_value |= (1 << 0);
  reg_value &= ~(1 << 7);
  WriteReg(0x01, reg_value);
  return afterSeconds * div;
}

int I2C_BM8563::SetAlarmIRQ(const I2C_BM8563_TimeTypeDef &I2C_BM8563_TimeStruct) {
  uint8_t irq_enable = false;
  uint8_t out_buf[4] = {0x80, 0x80, 0x80, 0x80};

  if (I2C_BM8563_TimeStruct.minutes >= 0) {
    irq_enable = true;
    out_buf[0] = byteToBcd2(I2C_BM8563_TimeStruct.minutes) & 0x7f;
  }

  if (I2C_BM8563_TimeStruct.hours >= 0) {
    irq_enable = true;
    out_buf[1] = byteToBcd2(I2C_BM8563_TimeStruct.hours) & 0x3f;
  }

  out_buf[2] = 0x80;
  out_buf[3] = 0x80;

  uint8_t reg_value = ReadReg(0x01);

  if (irq_enable) {
    reg_value |= (1 << 1);
  } else {
    reg_value &= ~(1 << 1);
  }

  for (int i = 0; i < 4; i++) {
    WriteReg(0x09 + i, out_buf[i]);
  }
  WriteReg(0x01, reg_value);

  return irq_enable ? 1 : 0;
}

int I2C_BM8563::SetAlarmIRQ(const I2C_BM8563_DateTypeDef &I2C_BM8563_DateStruct, const I2C_BM8563_TimeTypeDef &I2C_BM8563_TimeStruct) {
  uint8_t irq_enable = false;
  uint8_t out_buf[4] = {0x80, 0x80, 0x80, 0x80};

  if (I2C_BM8563_TimeStruct.minutes >= 0) {
    irq_enable = true;
    out_buf[0] = byteToBcd2(I2C_BM8563_TimeStruct.minutes) & 0x7f;
  }

  if (I2C_BM8563_TimeStruct.hours >= 0) {
    irq_enable = true;
    out_buf[1] = byteToBcd2(I2C_BM8563_TimeStruct.hours) & 0x3f;
  }

  if (I2C_BM8563_DateStruct.date >= 0) {
    irq_enable = true;
    out_buf[2] = byteToBcd2(I2C_BM8563_DateStruct.date) & 0x3f;
  }

  if (I2C_BM8563_DateStruct.weekDay >= 0) {
    irq_enable = true;
    out_buf[3] = byteToBcd2(I2C_BM8563_DateStruct.weekDay) & 0x07;
  }

  uint8_t reg_value = ReadReg(0x01);

  if (irq_enable) {
    reg_value |= (1 << 1);
  } else {
    reg_value &= ~(1 << 1);
  }

  for (int i = 0; i < 4; i++) {
    WriteReg(0x09 + i, out_buf[i]);
  }
  WriteReg(0x01, reg_value);

  return irq_enable ? 1 : 0;
}

void I2C_BM8563::clearIRQ() {
  uint8_t data = ReadReg(0x01);
  WriteReg(0x01, data & 0xf3);
}

void I2C_BM8563::disableIRQ() {
  clearIRQ();
  uint8_t data = ReadReg(0x01);
  WriteReg(0x01, data & 0xfC);
}