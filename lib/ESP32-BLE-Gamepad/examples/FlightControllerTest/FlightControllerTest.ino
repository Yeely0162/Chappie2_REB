/*
 * Flight controller test
 */

#include <Arduino.h>
#include <BleGamepad.h>

#define numOfButtons 32
#define numOfHatSwitches 0
#define enableX true
#define enableY true
#define enableZ false
#define enableRZ false
#define enableRX false
#define enableRY false
#define enableSlider1 false
#define enableSlider2 false
#define enableRudder true
#define enableThrottle true
#define enableAccelerator false
#define enableBrake false
#define enableSteering false

BleGamepad bleGamepad("BLE Flight Controller", "lemmingDev", 100);

void setup()
{
    Serial.begin(115200);
    Serial.println("Starting BLE work!");

    // Setup controller with 32 buttons, accelerator, brake and steering
    BleGamepadConfiguration bleGamepadConfig;
    bleGamepadConfig.setAutoReport(false);
    bleGamepadConfig.setControllerType(CONTROLLER_TYPE_MULTI_AXIS); // CONTROLLER_TYPE_JOYSTICK, CONTROLLER_TYPE_GAMEPAD (DEFAULT), CONTROLLER_TYPE_MULTI_AXIS
    bleGamepadConfig.setButtonCount(numOfButtons);
    bleGamepadConfig.setIncludeStart(true);
    bleGamepadConfig.setIncludeSelect(true);
    bleGamepadConfig.setWhichAxes(enableX, enableY, enableZ, enableRX, enableRY, enableRZ, enableSlider1, enableSlider2);      // Can also be done per-axis individually. All are true by default
    bleGamepadConfig.setWhichSimulationControls(enableRudder, enableThrottle, enableAccelerator, enableBrake, enableSteering); // Can also be done per-control individually. All are false by default
    bleGamepadConfig.setHatSwitchCount(numOfHatSwitches);                                                                      // 1 by default
    // Some non-Windows operating systems and web based gamepad testers don't like min axis set below 0, so 0 is set by default
    bleGamepadConfig.setAxesMin(0x8001); // -32767 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal
    bleGamepadConfig.setAxesMax(0x7FFF); // 32767 --> int16_t - 16 bit signed integer - Can be in decimal or hexadecimal 
    bleGamepad.begin(&bleGamepadConfig);

    // changing bleGamepadConfig after the begin function has no effect, unless you call the begin function again

    // Set throttle to min
    bleGamepad.setThrottle(-32767);

    // Set x and y axes and rudder to center
    bleGamepad.setX(0);
    bleGamepad.setY(0);
    bleGamepad.setRudder(0);
}

void loop()
{
    if (bleGamepad.isConnected())
    {
        Serial.println("Press all buttons one by one");
        for (int i = 1; i <= numOfButtons; i += 1)
        {
            bleGamepad.press(i);
            bleGamepad.sendReport();
            delay(100);
            bleGamepad.release(i);
            bleGamepad.sendReport();
            delay(25);
        }

        Serial.println("Press start and select");
        bleGamepad.pressStart();
        delay(100);
        bleGamepad.pressSelect();
        delay(100);
        bleGamepad.releaseStart();
        delay(100);
        bleGamepad.releaseSelect();

        Serial.println("Move x axis from center to max");
        for (int i = 0; i > -32767; i -= 256)
        {
            bleGamepad.setX(i);
            bleGamepad.sendReport();
            delay(10);
        }

        Serial.println("Move x axis from min to max");
        for (int i = -32767; i < 32767; i += 256)
        {
            bleGamepad.setX(i);
            bleGamepad.sendReport();
            delay(10);
        }

        Serial.println("Move x axis from max to center");
        for (int i = 32767; i > 0; i -= 256)
        {
            bleGamepad.setX(i);
            bleGamepad.sendReport();
            delay(10);
        }
        bleGamepad.setX(0);
        bleGamepad.sendReport();

        Serial.println("Move y axis from center to max");
        for (int i = 0; i > -32767; i -= 256)
        {
            bleGamepad.setY(i);
            bleGamepad.sendReport();
            delay(10);
        }

        Serial.println("Move y axis from min to max");
        for (int i = -32767; i < 32767; i += 256)
        {
            bleGamepad.setY(i);
            bleGamepad.sendReport();
            delay(10);
        }

        Serial.println("Move y axis from max to center");
        for (int i = 32767; i > 0; i -= 256)
        {
            bleGamepad.setY(i);
            bleGamepad.sendReport();
            delay(10);
        }
        bleGamepad.setY(0);
        bleGamepad.sendReport();

        Serial.println("Move rudder from min to max");
        // for(int i = 32767 ; i > -32767 ; i -= 256)    //Use this for loop setup instead if rudder is reversed
        for (int i = -32767; i < 32767; i += 256)
        {
            bleGamepad.setRudder(i);
            bleGamepad.sendReport();
            delay(10);
        }
        bleGamepad.setRudder(0);
        bleGamepad.sendReport();

        Serial.println("Move throttle from min to max");
        for (int i = -32767; i < 32767; i += 256)
        {
            bleGamepad.setThrottle(i);
            bleGamepad.sendReport();
            delay(10);
        }
        bleGamepad.setThrottle(-32767);
        bleGamepad.sendReport();
    }
}