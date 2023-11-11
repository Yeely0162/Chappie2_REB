#include "Seeed_BMP280.h"

bool BMP280::init(void)
{
    
  Wire.begin();
  // Wire.setClock(3400);
  Serial.println(Wire.getClock());
  if(bmp280Read8(BMP280_REG_CHIPID) != 0x58)
    return false;
  dig_T1 = bmp280Read16LE(BMP280_REG_DIG_T1);
  dig_T2 = bmp280ReadS16LE(BMP280_REG_DIG_T2);
  dig_T3 = bmp280ReadS16LE(BMP280_REG_DIG_T3);

  dig_P1 = bmp280Read16LE(BMP280_REG_DIG_P1);
  dig_P2 = bmp280ReadS16LE(BMP280_REG_DIG_P2);
  dig_P3 = bmp280ReadS16LE(BMP280_REG_DIG_P3);
  dig_P4 = bmp280ReadS16LE(BMP280_REG_DIG_P4);
  dig_P5 = bmp280ReadS16LE(BMP280_REG_DIG_P5);
  dig_P6 = bmp280ReadS16LE(BMP280_REG_DIG_P6);
  dig_P7 = bmp280ReadS16LE(BMP280_REG_DIG_P7);
  dig_P8 = bmp280ReadS16LE(BMP280_REG_DIG_P8);
  dig_P9 = bmp280ReadS16LE(BMP280_REG_DIG_P9);

  writeRegister(BMP280_REG_CONTROL, 0x3F);

  return true;
}

float BMP280::getTemperature(void)
{
  int32_t var1, var2;

  int32_t adc_T = bmp280Read24(BMP280_REG_TEMPDATA);
  adc_T >>= 4;
  var1 = (((adc_T >> 3) - ((int32_t)(dig_T1 << 1))) *
    ((int32_t)dig_T2)) >> 11;

  var2 = (((((adc_T >> 4) - ((int32_t)dig_T1)) *
    ((adc_T >> 4) - ((int32_t)dig_T1))) >> 12) *
    ((int32_t)dig_T3)) >> 14;

  t_fine = var1 + var2;
  float T = (t_fine * 5 + 128) >> 8;
  return T/100;
}

uint32_t BMP280::getPressure(void)
{
  int64_t var1, var2, p;

  // Call getTemperature to get t_fine
  getTemperature();

  int32_t adc_P = bmp280Read24(BMP280_REG_PRESSUREDATA);
  adc_P >>= 4;

  var1 = ((int64_t)t_fine) - 128000;
  var2 = var1 * var1 * (int64_t)dig_P6;
  var2 = var2 + ((var1*(int64_t)dig_P5)<<17);
  var2 = var2 + (((int64_t)dig_P4)<<35);
  var1 = ((var1 * var1 * (int64_t)dig_P3)>>8) + ((var1 * (int64_t)dig_P2)<<12);
  var1 = (((((int64_t)1)<<47)+var1))*((int64_t)dig_P1)>>33;
  if (var1 == 0)
  {
  return 0; // avoid exception caused by division by zero
  }
  p = 1048576-adc_P;
  p = (((p<<31)-var2)*3125)/var1;
  var1 = (((int64_t)dig_P9) * (p>>13) * (p>>13)) >> 25;
  var2 = (((int64_t)dig_P8) * p) >> 19;
  p = ((p + var1 + var2) >> 8) + (((int64_t)dig_P7)<<4);
  return (uint32_t)p/256;
}

float BMP280::calcAltitude(float pressure)
{
  float A = pressure/101325;
  float B = 1/5.25588;
  float C = pow(A,B);
  C = 1.0 - C;
  C = C /0.0000225577;
  return C;
}

uint8_t BMP280::bmp280Read8(uint8_t reg)
{
  Wire.beginTransmission(BMP280_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();
  Wire.requestFrom(BMP280_ADDRESS, 1);
  while(!Wire.available());
  return Wire.read();
}

uint16_t BMP280::bmp280Read16(uint8_t reg)
{
  uint8_t msb, lsb;

  Wire.beginTransmission(BMP280_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();

  Wire.requestFrom(BMP280_ADDRESS, 2);
  while(Wire.available()<2);
  msb = Wire.read();
  lsb = Wire.read();

  return (uint16_t) msb<<8 | lsb;
}

uint16_t BMP280::bmp280Read16LE(uint8_t reg)
{
  uint16_t data = bmp280Read16(reg);
  return (data >> 8) | (data << 8);
}

int16_t BMP280::bmp280ReadS16(uint8_t reg)
{
  return (int16_t)bmp280Read16(reg);
}

int16_t BMP280::bmp280ReadS16LE(uint8_t reg)
{
  return (int16_t)bmp280Read16LE(reg);
}

uint32_t BMP280::bmp280Read24(uint8_t reg)
{
  uint32_t data;

  Wire.beginTransmission(BMP280_ADDRESS);
  Wire.write(reg);
  Wire.endTransmission();

  Wire.requestFrom(BMP280_ADDRESS, 3);
  while(Wire.available()<3);
  data = Wire.read();
  data <<= 8;
  data |= Wire.read();
  data <<= 8;
  data |= Wire.read();

  return data;
}

void BMP280::writeRegister(uint8_t reg, uint8_t val)
{
  Wire.beginTransmission(BMP280_ADDRESS); // start transmission to device
  Wire.write(reg);       // send register address
  Wire.write(val);         // send value to write
  delay(10);
  Wire.endTransmission();     // end transmission
}
