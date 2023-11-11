/**
 * @file main.cpp
 * @author Forairaaaaa
 * @authors Yeely - Rebuild
  * @authors Kalicyh - Rebuild
 * @brief 
 * @version 0.1
 * @date 2023-03-02
 * 
 * @copyright Copyright (c) 2023
 * 
 */
#include "Arduino.h"
#include "ChappieUI/ChappieUI.h"

CHAPPIEUI ChappieUI;

void setup()
{
    ChappieUI.begin();
}

void loop()
{
    ChappieUI.update();
}
