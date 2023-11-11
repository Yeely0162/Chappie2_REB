#include "AXP173.h"


/* Private functions */
inline uint16_t AXP173::_getMin(uint16_t a, uint16_t b) {
    return ((a)<(b)?(a):(b));
}

inline uint16_t AXP173::_getMax(uint16_t a, uint16_t b) {
    return ((a)>(b)?(a):(b));
}

uint16_t AXP173::_getMid(uint16_t input, uint16_t min, uint16_t max) {
    return _getMax(_getMin(input, max), min);
}


/* Public functions */
#ifdef ARDUINO
/**
 * @brief AXP173 init
 * 
 * @param wire I2C wire pointer
 * @return true Init failed
 * @return false Init successful
 */
bool AXP173::begin() {
    Wire.begin();
    // Wire.beginTransmission(0x34);
    // return Wire.endTransmission() ? true : false;
    return true;
}
#else
void AXP173::begin() {
}
#endif

bool AXP173::isACINExist() {
    return ( read8Bit(0x00) & 0B10000000 ) ? true : false;
}

bool AXP173::isACINAvl() {
    return ( read8Bit(0x00) & 0B01000000 ) ? true : false;
}

bool AXP173::isVBUSExist() {
    return ( read8Bit(0x00) & 0B00100000 ) ? true : false;
}

bool AXP173::isVBUSAvl() {
    return ( read8Bit(0x00) & 0B00010000 ) ? true : false;
}

/**
 * @brief Get bat current direction
 * 
 * @return true Bat charging
 * @return false Bat discharging
 */
bool AXP173::getBatCurrentDir() {
    return ( read8Bit(0x00) & 0B00000100 ) ? true : false;
}

bool AXP173::isAXP173OverTemp() {
    return ( read8Bit(0x01) & 0B10000000 ) ? true : false;
}

/**
 * @brief Get bat charging state
 * 
 * @return true Charging
 * @return false Charge finished or not charging
 */
bool AXP173::isCharging() {
    return ( read8Bit(0x01) & 0B01000000 ) ? true : false;
}

bool AXP173::isBatExist() {
    return ( read8Bit(0x01) & 0B00100000 ) ? true : false;
}

bool AXP173::isChargeCsmaller() {
    return ( read8Bit(0x01) & 0B00000100 ) ? true : false;
}

/**
 * @brief Set channels' output enable or disable
 * 
 * @param channel Output channel
 * @param state true:Enable, false:Disable
 */
void AXP173::setOutputEnable(OUTPUT_CHANNEL channel, bool state) {
    uint8_t buff = read8Bit(0x12);
    buff = state ? ( buff | (1U << channel) ) : ( buff & ~(1U << channel) );
    write1Byte(0x12, buff);
}

/**
 * @brief Set channels' output voltage
 * 
 * @param channel Output channel
 * @param voltage DCDC1 & LDO4: 700~3500(mV), DCDC2: 700~2275(mV), LDO2 & LDO3: 1800~3300{mV}
 */
void AXP173::setOutputVoltage(OUTPUT_CHANNEL channel, uint16_t voltage) {
    uint8_t buff = 0;
    switch (channel) {
        case OP_DCDC1:
            voltage = (_getMid(voltage, 700, 3500) - 700) / 25;
            buff = read8Bit(0x26);
            buff = (buff & 0B10000000) | (voltage & 0B01111111);
            write1Byte(0x26, buff);
            break;
        case OP_DCDC2:
            voltage = (_getMid(voltage, 700, 2275) - 700) / 25;
            buff = read8Bit(0x23);
            buff = (buff & 0B11000000) | (voltage & 0B00111111);
            write1Byte(0x23, buff);
            break;
        case OP_LDO2:
            voltage = (_getMid(voltage, 1800, 3300) - 1800) / 100;
            buff = read8Bit(0x28);
            buff = (buff & 0B00001111) | (voltage << 4);
            write1Byte(0x28, buff);
            break;
        case OP_LDO3:
            voltage = (_getMid(voltage, 1800, 3300) - 1800) / 100;
            buff = read8Bit(0x28);
            buff = (buff & 0B11110000) | (voltage);
            write1Byte(0x28, buff);
            break;
        case OP_LDO4:
            voltage = (_getMid(voltage, 700, 3500) - 700) / 25;
            buff = read8Bit(0x27);
            buff = (buff & 0B10000000) | (voltage & 0B01111111);
            write1Byte(0x27, buff);
            break;
        default:
            break;
    }
}

void AXP173::powerOFF() {
    write1Byte(0x32, (read8Bit(0x32) | 0B10000000));
}

/**
 * @brief Set charge enable or disable
 * 
 * @param state true:Enable, false:Disable
 */
void AXP173::setChargeEnable(bool state) {
    if (state)
        write1Byte(0x33, ((read8Bit(0x33) | 0B10000000)));
    else
        write1Byte(0x33, ((read8Bit(0x33) & 0B01111111)));
}

void AXP173::setChargeCurrent(CHARGE_CURRENT current) {
    write1Byte(0x33, ((read8Bit(0x33) & 0B11110000) | current));
}

/**
 * @brief Set ADC channel enable or disable
 * 
 * @param channel ADC channel
 * @param state true:Enable, false:Disable
 */
void AXP173::setADCEnable(ADC_CHANNEL channel, bool state) {
    uint8_t buff = read8Bit(0x82);
    buff = state ? ( buff | (1U << channel) ) : ( buff & ~(1U << channel) );
    write1Byte(0x82, buff);
}

void AXP173::setCoulometer(COULOMETER_CTRL option, bool state) {
    uint8_t buff = read8Bit(0xB8);
    buff = state ? ( buff | (1U << option) ) : ( buff & ~(1U << option) );
    write1Byte(0xB8, buff);
}

inline uint32_t AXP173::getCoulometerChargeData() {
    return read32Bit(0xB0);
}

inline uint32_t AXP173::getCoulometerDischargeData() {
    return read32Bit(0xB4);
}

float AXP173::getCoulometerData() {
    uint32_t coin = getCoulometerChargeData();
    uint32_t coout = getCoulometerDischargeData();
    // data = 65536 * current_LSB * (coin - coout) / 3600 / ADC rate
    return 65536 * 0.5 * (int32_t)(coin - coout) / 3600.0 / 25.0;
}

float AXP173::getBatVoltage() {
    float ADCLSB = 1.1 / 1000.0;
    return read12Bit(0x78) * ADCLSB;
}

float AXP173::getBatCurrent() {
    float ADCLSB = 0.5;
    uint16_t CurrentIn = read13Bit(0x7A);
    uint16_t CurrentOut = read13Bit(0x7C);
    // uint16_t CurrentIn = read24Bit(0x7A);  // Modified By Yeely
    // uint16_t CurrentOut = read24Bit(0x7C); // Modified By Yeely
    return (CurrentIn - CurrentOut) * ADCLSB;
}
/**
 * @brief Get Battery DisCharing Current
 * 
 * @param return battery discharing current
 */
float AXP173::getBatDisCurrent(){
    float ADCLSB = 0.5;
    return (uint16_t) read24Bit(0x7C) * ADCLSB;
}
float AXP173::getBatLevel() {
    const float batVoltage = getBatVoltage();
    const float batPercentage = (batVoltage < 3.248088) ? (0) : (batVoltage - 3.120712) * 100;       
    return (batPercentage <= 100) ? batPercentage : 100;    
}

float AXP173::getBatPower() {
    float VoltageLSB = 1.1;
    float CurrentLCS = 0.5;
    uint32_t ReData = read24Bit(0x70);
    // uint32_t ReData = read8Bit(0x70); // Modified By Yeely
    return VoltageLSB * CurrentLCS * ReData / 1000.0;    
}

float AXP173::getVBUSVoltage() {
    float ADCLSB = 1.7 / 1000.0;
    uint16_t ReData = read12Bit(0x5A);
    // uint16_t ReData = read32Bit(0x5A); // Modified By Yeely
    return ReData * ADCLSB;
}

float AXP173::getVBUSCurrent() {
    float ADCLSB = 0.375;
    uint16_t ReData = read12Bit(0x5C);
    // uint16_t ReData = read24Bit(0x5C);// Modified By Yeely
    return ReData * ADCLSB;
}

float AXP173::getAXP173Temp() {
    float ADCLSB = 0.1;
    const float OFFSET_DEG_C = -144.7;
    uint16_t ReData = read12Bit(0x5E);
    return OFFSET_DEG_C + ReData * ADCLSB;
}

float AXP173::getTSTemp() {
    float ADCLSB = 0.1;
    const float OFFSET_DEG_C = -144.7;
    uint16_t ReData = read12Bit(0x62);
    return OFFSET_DEG_C + ReData * ADCLSB;
}
void AXP173::write1Byte(uint8_t addr, uint8_t data) {
    Wire.beginTransmission(0x34);
    Wire.write(addr);
    Wire.write(data);
    Wire.endTransmission();
}

uint8_t AXP173::read8Bit(uint8_t addr) {
    Wire.beginTransmission(0x34);
    Wire.write(addr);
    Wire.endTransmission();
    Wire.requestFrom(0x34, (size_t)1);
    return Wire.read();
}

uint16_t AXP173::read12Bit(uint8_t addr) {
    uint8_t buff[2];
    readBuff(addr, 2, buff);
    return (buff[0] << 4) + buff[1];
}

uint16_t AXP173::read13Bit(uint8_t addr) {
    uint8_t buff[2];
    readBuff(addr, 2, buff);
    return (buff[0] << 5) + buff[1];
}

uint16_t AXP173::read16Bit(uint8_t addr) {
    uint8_t buff[2];
    readBuff(addr, 2, buff);
    return (buff[0] << 8) + buff[1];
}

uint32_t AXP173::read24Bit(uint8_t addr) {
    uint8_t buff[4];
    readBuff(addr, 3, buff);
    return (buff[0] << 16) + (buff[1] << 8) + buff[2];
}

uint32_t AXP173::read32Bit(uint8_t addr) {
    uint8_t buff[4];
    readBuff(addr, 4, buff);
    return (buff[0] << 24) + (buff[1] << 16) + (buff[2] << 8) + buff[3];
}

void AXP173::readBuff(uint8_t addr, int size, uint8_t buff[]) {
    Wire.beginTransmission(0x34);
    Wire.write(addr);
    Wire.endTransmission();
    Wire.requestFrom(0x34, (size_t)size);
    for (int i = 0; i < size; i++) {
        buff[i] = Wire.read();
    }
}

uint16_t AXP173::read16Bit_lowFirst(uint8_t addr) {
    uint8_t buff[2];
    readBuff(addr, 2, buff);
    return buff[0] + (buff[1] << 8);
}

void AXP173::write16Bit(uint8_t addr, uint8_t data_1, uint8_t data_2) {
    Wire.beginTransmission(0x34);
    Wire.write(addr);
    Wire.write(data_1);
    Wire.write(data_2);
    Wire.endTransmission();
}
