/**
 * @file ChappiePower.hpp
 * @author Forairaaaaa / Yeely
 * @brief AXP173 power management with a filtered battery gauge.
 *
 * The AXP173 has no fuel gauge. The reported percentage is a *voltage-based
 * estimate* on a Li-ion curve, filtered over time. Accuracy is limited by cell
 * chemistry, load transients and temperature; it is not a true state of charge.
 */
#pragma once
#include <Arduino.h>
#include <math.h>
#include "AXP173/AXP173.h"

#define AXPManage 1
#define IP5Manage 0
#define CHAPPIE_PWR_CTRL    GPIO_NUM_9
#define CHAPPIE_BATM_ADC    GPIO_NUM_8
#define CHAPPIE_BATM_EN     GPIO_NUM_18
#define POW_LOG(format, args...) printf(format, ##args)

class ChappiePower {
    private:
        AXP173 pmu;

        /* Filtered snapshot */
        bool _hasSample = false;
        bool _valid = false;
        bool _charging = false;
        bool _vbus = false;
        bool _acin = false;
        bool _present = false;
        bool _ioOk = false;
        float _voltage = 0.0f;
        float _currentMA = 0.0f;
        float _percentF = 0.0f;
        uint8_t _percent = 0;
        float _vbusV = 0.0f;
        float _vbusMA = 0.0f;
        float _pmicTemp = 0.0f;
        uint32_t _lastSample = 0;
        uint8_t _badSamples = 0;

        inline void _enableBatMeasure() { digitalWrite(CHAPPIE_BATM_EN, 0); }
        inline void _disableBatMeasure() { digitalWrite(CHAPPIE_BATM_EN, 1); }

        /**
         * @brief Piecewise Li-ion open-circuit approximation for a small
         *        350 mAh cell. Interpolated between anchor points.
         */
        static uint8_t _voltageToPercent(float v)
        {
            static const struct { float v; uint8_t p; } curve[] = {
                {3.20f,   0}, {3.40f,   5}, {3.55f,  15}, {3.65f,  30},
                {3.72f,  45}, {3.78f,  60}, {3.85f,  72}, {3.92f,  82},
                {4.00f,  90}, {4.08f,  96}, {4.20f, 100},
            };
            const size_t n = sizeof(curve) / sizeof(curve[0]);
            if (v <= curve[0].v) return 0;
            if (v >= curve[n - 1].v) return 100;
            for (size_t i = 1; i < n; i++) {
                if (v <= curve[i].v) {
                    float span = curve[i].v - curve[i - 1].v;
                    float t = (v - curve[i - 1].v) / span;
                    return (uint8_t)lroundf(curve[i - 1].p + t * (curve[i].p - curve[i - 1].p));
                }
            }
            return 100;
        }

    public:
        ChappiePower() {}
        ~ChappiePower() {}

        inline void CloseLDO()
        {
            #if AXPManage
                pmu.setOutputEnable(pmu.OP_LDO2, false);
                pmu.setOutputEnable(pmu.OP_LDO3, false);
            #endif
        }

        inline void init()
        {
            #if IP5Manage
                gpio_reset_pin(CHAPPIE_BATM_ADC);
                gpio_reset_pin(CHAPPIE_BATM_EN);
                gpio_set_direction(CHAPPIE_BATM_EN, GPIO_MODE_OUTPUT_OD);
                analogReadResolution(12);
            #endif

            #if AXPManage
                bool axpInit = pmu.begin();
                printf(axpInit ? "[POW] AXP173 ready\n" : "[POW] AXP173 not responding\n");

                if (axpInit) {
                    pmu.setOutputEnable(pmu.OP_LDO2, true);
                    pmu.setOutputVoltage(pmu.OP_LDO2, 2000);
                    pmu.setOutputEnable(pmu.OP_LDO3, true);
                    pmu.setOutputVoltage(pmu.OP_LDO3, 2000);

                    pmu.setADCEnable(pmu.ADC_TS, false);      /* no external TS network */
                    pmu.setADCEnable(pmu.ADC_BAT_C, true);
                    pmu.setADCEnable(pmu.ADC_BAT_V, true);
                    pmu.setADCEnable(pmu.ADC_VBUS_C, true);
                    pmu.setADCEnable(pmu.ADC_VBUS_V, true);
                }

                if (axpInit) {
                    POW_LOG("[POW] PMIC %.1fC  VBUS %.2fV %.0fmA  Bat %.2fV %.0fmA\n",
                            pmu.getAXP173Temp(), pmu.getVBUSVoltage(), pmu.getVBUSCurrent(),
                            pmu.getBatVoltage(), pmu.getBatCurrentSignedMA());
                    update(true);
                } else {
                    _valid = false;
                }
            #endif
        }

        /* ------------------------------------------------------------------ */
        /* Sampled state                                                       */
        /* ------------------------------------------------------------------ */

        /**
         * @brief Sample the PMIC and update the filtered gauge. Call from the
         *        main loop task only. Cheap enough for a 2 s interval.
         */
        inline void update(bool force = false)
        {
            #if AXPManage
                const uint32_t now = millis();
                if (!force && _hasSample && (now - _lastSample) < 2000)
                    return;
                _lastSample = now;

                const float rawV = pmu.getBatVoltage();
                _ioOk = pmu.lastIoOk();
                if (!_ioOk || !isfinite(rawV) || rawV < 2.0f || rawV > 4.6f) {
                    if (_badSamples < 255) _badSamples++;
                    if (_badSamples >= 3)
                        _valid = false;
                    return;
                }
                _badSamples = 0;
                _valid = true;

                const bool charging = pmu.isCharging();
                const float rawI = charging ? pmu.getBatChargeCurrentMA()
                                            : -pmu.getBatDischargeCurrentMA();
                const bool transition = _hasSample && (charging != _charging);

                if (!_hasSample) {
                    _voltage = rawV;
                    _currentMA = rawI;
                    _percentF = _voltageToPercent(rawV);
                    _percent = (uint8_t)lroundf(_percentF);
                    _hasSample = true;
                } else {
                    /* Voltage is slow-filtered; a state transition adapts faster
                     * so plug/unplug does not leave a stale percentage. */
                    const float alphaV = transition ? 0.5f : 0.12f;
                    _voltage += alphaV * (rawV - _voltage);
                    _currentMA += 0.30f * (rawI - _currentMA);

                    const float target = (float)_voltageToPercent(_voltage);
                    const float alphaP = transition ? 0.5f : 0.12f;
                    _percentF += alphaP * (target - _percentF);
                    if (_percentF < 0.0f) _percentF = 0.0f;
                    if (_percentF > 100.0f) _percentF = 100.0f;

                    uint8_t shown = (uint8_t)lroundf(_percentF);
                    /* Hysteresis: ignore sub-1% wobble. */
                    if (shown != _percent)
                        _percent = shown;
                }

                _charging = charging;
                _vbus = pmu.isVBUSExist();
                _acin = pmu.isACINExist();
                _present = pmu.isBatExist();
                _vbusV = pmu.getVBUSVoltage();
                _vbusMA = pmu.getVBUSCurrent();
                _pmicTemp = pmu.getAXP173Temp();
            #endif
        }

        /* ------------------------------------------------------------------ */
        /* Query API                                                          */
        /* ------------------------------------------------------------------ */

        inline bool dataValid() const { return _valid; }
        inline bool isCharging() const { return _charging; }
        inline bool batteryPresent() const { return _present; }
        inline bool vbusPresent() const { return _vbus; }
        inline bool acinPresent() const { return _acin; }
        inline bool ioOk() const { return _ioOk; }
        inline uint8_t percent() const { return _percent; }
        inline uint8_t batPercent() const { return _percent; }
        inline float voltageV() const { return _voltage; }
        inline float currentMA() const { return _currentMA; }
        inline float vbusVoltageV() const { return _vbusV; }
        inline float vbusCurrentMA() const { return _vbusMA; }
        inline float pmicTempC() const { return _pmicTemp; }

        /**
         * @brief Full / charging / discharging / idle. "Full" requires a valid
         *        charge-status register plus a near-4.2 V pack and low current.
         */
        inline const char* chargeText() const
        {
            if (!_present) return "No battery";
            if (_charging) {
                if (_voltage >= 4.15f && _currentMA < 60.0f) return "Full";
                return "Charging";
            }
            if (_vbus || _acin) return "External power";
            return "Discharging";
        }

        /* ------------------------------------------------------------------ */
        /* Legacy getters (existing UI used String concatenation)             */
        /* ------------------------------------------------------------------ */

        inline float readBatVoltage() { return (float)_percent; }   /* legacy: percent */
        inline uint8_t readBatPercentage() { return _percent; }

        inline bool isCharing() { return _charging; }
        inline bool isChargedBat() { return _present; }

        inline String AXP173Temp()
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%.1f", _pmicTemp);
            return String(buf);
        }
        inline String USBVoltage()
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%.2f", _vbusV);
            return String(buf);
        }
        inline String USBCurrent()
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%.0f", _vbusMA);
            return String(buf);
        }
        inline String BatVoltage()
        {
            char buf[12];
            snprintf(buf, sizeof(buf), "%.2f", _voltage);
            return String(buf);
        }
        inline String BatCurrent()
        {
            char buf[14];
            snprintf(buf, sizeof(buf), "%.0f", _currentMA);
            return String(buf);
        }
        inline String BatPower()
        {
            char buf[14];
            snprintf(buf, sizeof(buf), "%.2f", fabsf(_voltage * _currentMA) / 1000.0f);
            return String(buf);
        }

        /* ------------------------------------------------------------------ */
        /* Charge configuration (verified AXP173 register 0x33 fields)        */
        /* ------------------------------------------------------------------ */

        inline uint8_t chargeCurrentLevel() { return pmu.getChargeCurrentCode(); }
        inline float chargeCurrentSettingMA() { return pmu.getChargeCurrentMA(); }
        inline float chargeVoltageSettingV() { return pmu.getChargeVoltageSettingV(); }

        static uint16_t chargeCurrentMAForLevel(uint8_t level)
        {
            static const uint16_t table[16] = {
                100, 190, 280, 360, 450, 550, 630, 700,
                780, 880, 960, 1000, 1080, 1160, 1240, 1320,
            };
            return table[level & 0x0F];
        }

        /**
         * @brief Set the charge current step (0..15 -> 100..1320 mA) and read it
         *        back. Returns false when the readback does not match.
         */
        inline bool setChargeCurrentLevel(uint8_t level)
        {
            #if AXPManage
                if (level > 15) return false;
                pmu.setChargeCurrent((AXP173::CHARGE_CURRENT)level);
                uint8_t rb = pmu.getChargeCurrentCode();
                return pmu.lastIoOk() && (rb == level);
            #else
                return false;
            #endif
        }

        /* ------------------------------------------------------------------ */
        /* Shutdown                                                           */
        /* ------------------------------------------------------------------ */

        /**
         * @brief Ask the AXP173 to cut the system rails. Returns after the
         *        command; on boards where the PMIC cannot cut its own input the
         *        caller must fall back to a low-power state.
         */
        inline void powerOff()
        {
            #if IP5Manage
                gpio_reset_pin(CHAPPIE_PWR_CTRL);
                gpio_set_direction(CHAPPIE_PWR_CTRL, GPIO_MODE_OUTPUT_OD);
                gpio_set_level(CHAPPIE_PWR_CTRL, 0);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 1);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 0);
                delay(100);
                gpio_set_level(CHAPPIE_PWR_CTRL, 1);
                delay(1000);
            #endif
            #if AXPManage
                POW_LOG("[POW] AXP173 power off command\n");
                pmu.powerOFF();
                delay(500);
            #endif
        }

        inline uint32_t readBatMilliVoltRaw()
        {
            gpio_set_direction(CHAPPIE_BATM_EN, GPIO_MODE_OUTPUT_OD);
            gpio_set_level(CHAPPIE_BATM_EN, 0);
            delay(10);
            uint32_t ret = analogReadMilliVolts(CHAPPIE_BATM_ADC);
            gpio_set_level(CHAPPIE_BATM_EN, 1);
            gpio_reset_pin(CHAPPIE_BATM_EN);
            gpio_reset_pin(CHAPPIE_BATM_ADC);
            return ret;
        }
};
