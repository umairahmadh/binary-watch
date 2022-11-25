/* 
 *  NOTE! 
 *        This code is NOT finished and will be subject to change.
 *        Bugs and unexpected behavior may occur.
 *  
 *   
 *  The Ultimate Binary Watch Code
 *  
 *  Button Description:
 *  
 *   -Top Button Press:     Wake up / Cycle between "Display Time", "Display Date", "Stopwatch" and "Alarm" modes.
 *   -Top Button Hold:      Enter "Set Time", "Set Date", "Start Stopwatch" or "Set Alarm" mode.
 *   -Bottom Button Press:  Increase Brightness.
 *   -Bottom Button Hold:   Enter "Choose Color" Mode or Reset Stopwatch.
 *  
 */

#include <Wire.h>
#include <FastLED.h>
#include "M41T62.h"
#include "LowPower.h"
#include "String.h"

// Create RTC object
RTC_M41T62 rtc;

// Physical Pin connections
const int16_t alarm_in_pin = 2, top_button_pin = 3, bott_button_pin = 4, display_ctrl_pin = 7, alarm_out_pin = 12, batt_sense_pin = A0; 
const int16_t number_of_leds = 16, data_pin = 6, clk_pin = 5; // For FastLED
CRGB leds[number_of_leds];

const int16_t wake_time = 5000; // How long to be awake before going to sleep 
uint32_t awake_counter = 0; //

int16_t brightness = 5;  // Global LED brightness
int16_t time_color[] = {0, 0, 255}; // Standard RGB color for displaying the time
int16_t date_color[] = {0, 255, 0}; // Standard RGB color for displaying the date
int16_t stopwatch_color[] = {0, 255, 0}; // Standard RGB color for the stopwatch mode
const int16_t edit_color[] = {255, 0, 0}; // Standard RGB color for the "set" modes (Set time, Set date, etc.)
const int16_t cursor_color[] = {150, 255, 255};  // Standard RGB color for indicating which column you are in

int16_t hours, minutes;
int16_t hours_tens, hours_ones, minutes_tens, minutes_ones;

int16_t days, months, years = 2020;
int16_t months_tens, months_ones, days_tens, days_ones;

uint32_t timer = 0;
uint32_t stopwatch_minutes = 0, tenSeconds = 0, stopwatch_seconds = 0, hundredMilliseconds = 0;
uint32_t prevTimer = 0;
uint32_t lastMillis = 0;
bool reset = true;

String mode = ""; // Keep track of which mode the watch is in

void setup() {
  
  Wire.begin();
  rtc.begin();
  
  FastLED.addLeds<APA102, data_pin, clk_pin, BGR>(leds, number_of_leds).setCorrection(TypicalLEDStrip); // LED Config
  FastLED.setBrightness(brightness); // Set global brightness

  // Set the RTC to the date & time this sketch was compiled
  rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
   
  // To set custom date & time you would call
  // rtc.adjust(DateTime(year, month, day, hour, minute, second));

  // Pin config
  pinMode(batt_sense_pin, INPUT); // Battery voltage sense
  pinMode(display_ctrl_pin, OUTPUT);  // Display ON/OFF pin for decreasing standby current consumption
  pinMode(alarm_out_pin, OUTPUT); // To vibration motor or buzzer
  pinMode(data_pin, OUTPUT);  // For FastLED
  pinMode(clk_pin, OUTPUT);  // For FastLED
  pinMode(bott_button_pin, INPUT_PULLUP); // Bottom button is active LOW
  pinMode(top_button_pin, INPUT_PULLUP);  // Top button is active LOW
  pinMode(alarm_in_pin, INPUT_PULLUP);  // External interrupt from RTC Alarm

  // All unconnected pins are pulled HIGH
  pinMode(0, INPUT_PULLUP);
  pinMode(1, INPUT_PULLUP);
  pinMode(8, INPUT_PULLUP);
  pinMode(9, INPUT_PULLUP);
  pinMode(10, INPUT_PULLUP);
  pinMode(11, INPUT_PULLUP);
  pinMode(13, INPUT_PULLUP);
  pinMode(A1, INPUT_PULLUP);
  pinMode(A2, INPUT_PULLUP);
  pinMode(A3, INPUT_PULLUP);

  // A4 & A5 are I2C pins used by the RTC
}

// Things to do after waking up from sleep
void WakeUp() {

  while (WaitForButtonRelease(top_button_pin) == true) {  
    // Wait for button release
  }

  // Reset necessary wake up conditions 
  detachInterrupt(1);
  pinMode(data_pin, OUTPUT);
  pinMode(clk_pin, OUTPUT);
  digitalWrite(display_ctrl_pin, HIGH);
  digitalWrite(alarm_out_pin, LOW);
  FastLED.setBrightness(brightness);
  awake_counter = millis();
  return;
}

void loop() {
  DisplayTime();
}

// Display time mode
void DisplayTime() {
  LowBatteryWarning(BatteryStatus()); // Check battery status and give a warning accordingly
  
  while (digitalRead(top_button_pin) == HIGH) { 
    mode = "Time";
    readRTC();  // Read the current time

    // Write the time to the LEDs
    Display(hours_tens, 0, 3, time_color[0], time_color[1], time_color[2]);
    Display(hours_ones, 4, 7, time_color[0], time_color[1], time_color[2]);
    Display(minutes_tens, 8, 11, time_color[0], time_color[1], time_color[2]);
    Display(minutes_ones, 12, 15, time_color[0], time_color[1], time_color[2]);

    if (digitalRead(bott_button_pin) == LOW) {

      if (WaitForButtonRelease(bott_button_pin) == true) {  // Bottom Button Hold: Go to "Choose Color" mode
        ChooseColor(mode);
        awake_counter = millis();
        delay(15); // Button debounce delay
        awake_counter = millis();
      } else {                                // Bottom Button Press: Increase Brightness
        ChangeBrightness();
        awake_counter = millis();
      }
    }

    TestForSleep(); // Check if it is time to sleep
  }

  if (WaitForButtonRelease(top_button_pin) == true) {   // Top Button Hold: Go to "Set Time" Mode
    SetTime();
    awake_counter = millis();
  } else {                                  // Top Button Press: Go to "Display Date" Mode
    DisplayDate();
    awake_counter = millis();
    return;
  }
}

// Set Time Mode
void SetTime() {
  readRTC();  // Read the current time
  ChangeColor(hours_tens, hours_ones, minutes_tens, minutes_ones, edit_color[0], edit_color[1], edit_color[2]);  // Change color to indicate "Edit Mode"

  int16_t column_counter = 1;  // Keep track of which column of LEDs is selected

  while (WaitForButtonRelease(top_button_pin) == false) { // Top Button Hold: Save and Exit

    if (digitalRead(bott_button_pin) == LOW) {

      if (WaitForButtonRelease(bott_button_pin) == false) { // Bottom Button Press: Add digit

        if (column_counter == 1) { // First column should not exceed 0-2 (24h-format)
          hours_tens++;
          if (hours_tens > 2) {
            hours_tens = 0;
          }
          Display(hours_tens, 0, 3, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else if (column_counter == 2) { // Second column could be either 0-4 or 0-9 (Depends on the first column)
          hours_ones++;
          if (hours_tens == 2) {
            if (hours_ones > 4) {
              hours_ones = 0;
            }
          } else {
            if (hours_ones > 9) {
              hours_ones = 0;
            }
          }
          Display(hours_ones, 4, 7, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else if (column_counter == 3) {  // Third column should not exceed 0-5 (59 minutes)
          minutes_tens++;
          if (minutes_tens > 5) {
            minutes_tens = 0;
          }
          Display(minutes_tens, 8, 11, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else {  // Fourth column should not exceed 0-9
          minutes_ones++;
          if (minutes_ones > 9) {
            minutes_ones = 0;
          }
          Display(minutes_ones, 12, 15, cursor_color[0], cursor_color[1], cursor_color[2]);
        }
      }
    }

    if (digitalRead(top_button_pin) == LOW) { // Top button press: Go to next column
      column_counter++;
      if (column_counter > 4) {
        column_counter = 1;
      }
    }
  } 

  // Calculate the new time
  int16_t newHour = (hours_tens * 10) + hours_ones;
  int16_t newMinute = (minutes_tens * 10) + minutes_ones;

  readRTC();  // Get current date since both time & date need to be written at the same time
  rtc.adjust(DateTime(years, months, days, newHour, newMinute, 0));  // Write new time to the RTC
  return;
}

// Read the time & date from the RTC
void readRTC() {

  DateTime now = rtc.now();

  hours = now.hour();
  minutes = now.minute();
  days = now.day();
  months = now.month();

  Split(hours, hours_tens, hours_ones);
  Split(minutes, minutes_tens, minutes_ones);
  Split(months, months_tens, months_ones);
  Split(days, days_tens, days_ones);
  return;
}
// Display Date Mode
void DisplayDate() {

  while (digitalRead(top_button_pin) == HIGH) { 
    mode = "Date";
    readRTC();  // Read date from the RTC

    // Write the date to the LEDs
    Display(days_tens, 0, 3, date_color[0], date_color[1], date_color[2]);
    Display(days_ones, 4, 7, date_color[0], date_color[1], date_color[2]);
    Display(months_tens, 8, 11, date_color[0], date_color[1], date_color[2]);
    Display(months_ones, 12, 15, date_color[0], date_color[1], date_color[2]);

    if (digitalRead(bott_button_pin) == LOW) {

      if (WaitForButtonRelease(bott_button_pin) == true) { // Bottom Button Hold: Go to "Choose Color" Mode
        ChooseColor(mode);
        delay(15); // Button debounce Delay
        awake_counter = millis();
      } else {                               // Bottom Button Press: Increase Brightness
        ChangeBrightness();
        awake_counter = millis();
      }
    }

    TestForSleep(); // Check if it is time to sleep
  }

  if (WaitForButtonRelease(top_button_pin) == true) { // Top Button Hold: Go to "Set Date" Mode
    SetDate();
    awake_counter = millis();
  } else {                    // Top Button Press: Go to "Stopwatch" Mode
    Stopwatch();
    awake_counter = millis();
    return;
  }
}

// Reset the stopwatch
void ResetStopwatch() {
  FastLED.clear();
  Display(1, 0, 3, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]); // Indicate stopwatch mode by a single LED in one corner
  return;
}

// YET TO BE WRITTEN!!
void DisplayAlarm() {
}

// YET TO BE WRITTEN!!
void SetAlarm() {
}

// Stopwatch mode
void Stopwatch() {

  if (reset == true) {
    ResetStopwatch();
  } else {
    Display(stopwatch_minutes, 0, 3, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
    Display(tenSeconds, 4, 7, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
    Display(stopwatch_seconds, 8, 11, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
    Display(hundredMilliseconds, 12, 15, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
  }

  while (digitalRead(top_button_pin) == HIGH) {

    if (digitalRead(bott_button_pin) == LOW) {   

      if (WaitForButtonRelease(bott_button_pin) == true) { // Bottom Button Hold: Go to "Choose Color" Mode
        if (reset == true) {
          ChooseColor("Stopwatch");
          delay(15); // Button debounce delay
        } else {                                // Bottom Button Hold: Reset Stopwatch              
          ResetStopwatch();     
          prevTimer = 0; 
          reset = true;
        }

      } else {                                // Bottom button press: Start Stopwatch
        lastMillis = millis();

        while (digitalRead(bott_button_pin) == HIGH) {

          timer = prevTimer + (millis() - lastMillis); 
          stopwatch_minutes = (timer / 1000) / 60;
          tenSeconds = ((timer / 10) / 1000) % 6; 
          stopwatch_seconds = ((timer / 1000) % 10);  
          hundredMilliseconds = (timer / 100) % 10;  

          if (stopwatch_minutes > 15) {   // Stop the counting when the minutes overflow
            return;
          } else {
            Display(stopwatch_minutes, 0, 3, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
            Display(tenSeconds, 4, 7, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
            Display(stopwatch_seconds, 8, 11, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
            Display(hundredMilliseconds, 12, 15, stopwatch_color[0], stopwatch_color[1], stopwatch_color[2]);
          }
        }

        if (WaitForButtonRelease(bott_button_pin) == true) {  
           // Wait
          }
        prevTimer = timer;
        reset = false;
      }
    }
  }
  
  if (WaitForButtonRelease(top_button_pin) == true) { 
  }
  awake_counter = millis();
  return;
}

// Choose color of LEDs
void ChooseColor(String mode) {

  int16_t rgb_state = 0, b, g, r;
  int16_t button_state = 1;

  // Start the loop from the current colors
  if (mode == "Time") {
    b = time_color[0];
    g = time_color[1];
    r = time_color[2];
  } else if (mode == "Date") {
    b = date_color[0];
    g = date_color[1];
    r = date_color[2];
  } else if (mode == "Stopwatch") {
    b = stopwatch_color[0];
    g = stopwatch_color[1];
    r = stopwatch_color[2];
  }

  while (digitalRead(top_button_pin) == HIGH) {

    if (digitalRead(bott_button_pin) == LOW) {
      while (digitalRead(bott_button_pin) == LOW)  {
        // Wait for button release
      }

      // Toggle the button state
      if (button_state == 0) {
        button_state = 1;
      } else {
        button_state = 0;
      }
    }

    // Cycle through colors
    if (button_state == 1) {

      if (rgb_state == 0) {
        if (g < 255) {
          g++;
        } else {
          rgb_state = 1;
        }
      }
      if (rgb_state == 1) {
        if (r > 0) {
          r--;
        } else {
          rgb_state = 2;
        }
      }
      if (rgb_state == 2) {
        if (b < 255) {
          b++;
        } else {
          rgb_state = 3;
        }
      }
      if (rgb_state == 3) {
        if (g > 0) {
          g--;
        } else {
          rgb_state = 4;
        }
      }
      if (rgb_state == 4) {
        if (r < 255) {
          r++;
        } else {
          rgb_state = 5;
        }
      }
      if (rgb_state == 5) {
        if (b > 0) {
          b--;
        } else {
          rgb_state = 0;
        }
      }

      for (int16_t i = 0; i < number_of_leds; i++) {
        leds[i].setRGB(b, g, r);
      }
      delay(5);
      FastLED.show();
    }
  }

  if (WaitForButtonRelease(top_button_pin) == true) {

    // Save the new color in the corresponding array
    if (mode == "Time") {
      time_color[0] = b;
      time_color[1] = g;
      time_color[2] = r;
    } else if (mode == "Date") {
      date_color[0] = b;
      date_color[1] = g;
      date_color[2] = r;
    } else if (mode == "Stopwatch") {
      stopwatch_color[0] = b;
      stopwatch_color[1] = g;
      stopwatch_color[2] = r;
    }
  } else {
    return;
  }
  return;
}

// Set Date Mode
void SetDate() {
  int16_t column_counter = 1; // Keep track of which column is selected

  readRTC(); // Read the current date from the RTC
  ChangeColor(days_tens, days_ones, months_tens, months_ones, edit_color[0], edit_color[1], edit_color[2]);   // Change color to indicate "Edit Mode"

  while (WaitForButtonRelease(top_button_pin) == false) {  // Top Button Hold: Save and exit

    if (digitalRead(bott_button_pin) == LOW) { // Bottom Button Press: Increase the selected digit

      if (WaitForButtonRelease(bott_button_pin) == false) {

        if (column_counter == 1) { // First column should not exceed 0-3 (32 days max)
          days_tens++;
          if (days_tens > 3) {
            days_tens = 0;
          }
          Display(days_tens, 0, 3, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else if (column_counter == 2) {  // Second column should be 0-2 or 0-9 (Depends on the first column)
          days_ones++;
          if (days_tens == 3) {
            if (days_ones > 2) {
              days_ones = 1;
            }
          } else {
            if (days_ones > 9) {
              days_ones = 1;
            }
          }
          Display(days_ones, 4, 7, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else if (column_counter == 3) {  // Third column should not exceed 1 (12 months max)
          months_tens++;
          if (months_tens > 1) {
            months_tens = 0;
          }
          Display(months_tens, 8, 11, cursor_color[0], cursor_color[1], cursor_color[2]);

        } else {  // Fourth column should be either 0-2 or 0-9 (Depends on the third column)
          months_ones++;
          if (months_tens == 1) {
            if (days_ones > 2) {
              days_ones = 0;
            }
          } else  {
            if (days_ones > 9) {
              days_ones = 0;
            }
          }
          Display(months_ones, 12, 15, cursor_color[0], cursor_color[1], cursor_color[2]);
        }
      }
    }
    if (digitalRead(top_button_pin) == LOW) { // Top Button press: Go to next column
      column_counter++;
      if (column_counter > 4) {
        column_counter = 1;
      }
    }
  }

  // Calculate a new date
  int16_t new_day = (days_tens * 10) + days_ones;
  int16_t new_month = (months_tens * 10) + months_ones;

  // Write new date to RTC
  readRTC(); // Get current time since both time and date need to be written at the same time
  rtc.adjust(DateTime(years, new_month, new_day, hours, minutes, 0)); // Write the date & time to the RTC
  return;
}

// Adjust global brightness
void ChangeBrightness() {
  if (brightness > 18) {
    brightness = 3;
  }
  brightness = brightness + 5;
  FastLED.setBrightness(brightness);
  return;
}

// Changes color of ALL LEDs to indicate which mode you´re in
void ChangeColor(int16_t first_col, int16_t second_col, int16_t third_col, int16_t fourth_col, int16_t new_red, int16_t new_green, int16_t new_blue) {
  Display(first_col, 0, 3, new_red, new_green, new_blue);
  Display(second_col, 4, 7, new_red, new_green, new_blue);
  Display(third_col, 8, 11, new_red, new_green, new_blue);
  Display(fourth_col, 12, 15, new_red, new_green, new_blue);
  return;
}

// Check battery level
int16_t BatteryStatus() {
  float batt_voltage = analogRead(batt_sense_pin) * (5.0 / 1023.0);  // Read battery voltage
  float level_1_limit = 3.5;  // Adjust limit levels if necessary 
  float level_2_limit = 3.3;

  if (batt_voltage < level_1_limit) {
    return 1;
  } else if (batt_voltage < level_2_limit) {
    return 2;
  } else {
    return 0;
  }
}

void LowBatteryWarning(int16_t level) {

  // Blink all LEDs red once if 3.0V < voltage < 3.5V 
  if (level == 1) {
    for (int16_t i = 0; i < number_of_leds; i++) {
      leds[i].setRGB(250, 0, 0);
    }
    FastLED.show();
    delay(200);
    for (int16_t i = 0; i < number_of_leds; i++) {
      leds[i].setRGB(0, 0, 0);
    }
    FastLED.show();
    delay(200);
    return;

  // Blink all LEDs red three times if voltage < 3.3V 
  } else if (level == 2) {

    for (int16_t j = 0; j < 3; j++) {

      for (int16_t i = 0; i < number_of_leds; i++) {
        leds[i].setRGB(250, 0, 0);
      }
      FastLED.show();
      delay(200);
      for (int16_t i = 0; i < number_of_leds; i++) {
        leds[i].setRGB(0, 0, 0);
      }
      FastLED.show();
      delay(200);
      return;
    }
  } else {
    return;
  }
}

// Check if it's time to sleep
void TestForSleep() {
  if (millis() - awake_counter >= wake_time) { 
    Sleep();
  } else {
    return;
  }
}

// Go to Sleep
void Sleep() {

  // Fade the LEDs
  for (int16_t i = brightness; i > 0; i--) {
    delay(20);
    FastLED.setBrightness(i);
    FastLED.show();

    // If any button is pressed... Wake Up!
    if (digitalRead(bott_button_pin) == LOW || digitalRead(top_button_pin) == LOW) { 
      FastLED.setBrightness(brightness);  

      while (digitalRead(bott_button_pin) == LOW || digitalRead(top_button_pin) == LOW) {
        // Wait for button release
      }
      awake_counter = millis();
      return;
    }
  }

  FastLED.clear();
  FastLED.show();

  digitalWrite(display_ctrl_pin, LOW); // Turn OFF the LEDs
  pinMode(data_pin, INPUT_PULLUP);  
  pinMode(clk_pin, INPUT_PULLUP);
  attachInterrupt(1, WakeUp, LOW); // Attach interrupt on digital pin 2
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
}

// Write to LEDs ( There is probably a way more effecient way to write this function!! )
void Display(int16_t value, int16_t start_LED, int16_t end_LED, int16_t R, int16_t G, int16_t B) {
  digitalWrite(display_ctrl_pin, HIGH);  // Turn on LEDs
  int16_t binary_arr[] = {0, 0, 0, 0};
  String valueBinary = String(ToBinary(value)); // Convert decimal number to binary integer
  String fourBitValueBinary = String("0000" + valueBinary);  // Extend "value binary" string to a minimum of 4bits

  for (int16_t i = fourBitValueBinary.length() - 1; i > 3; i--) {
    binary_arr[i - valueBinary.length()] = fourBitValueBinary[i] == '1' ? 1 : 0; // Fill the integer array with values from the binary string
  }

  for (int16_t i = end_LED; i >= start_LED; i--) {  // Write the data from the integer array to the LEDs
    if (binary_arr[end_LED - i] == 1) {
      leds[i].setRGB(R, G, B);  // If bit in array = 1, Turn LED ON
    } else {
      leds[i].setRGB(0, 0, 0);  // If bit in array = 0 Turn Led OFF
    }
  }
  FastLED.show(); // Show the results
  return;
}

// Check for button press/hold 
bool WaitForButtonRelease(int16_t button) {
  int16_t hold_time = 1000;  // Time difference between a "press" and a "hold"
  uint32_t prev_millis = millis();

  while (digitalRead(button) == LOW) {
    // Wait for button release
  }
  return (millis() - prev_millis >= hold_time); // Return true if it is a "button hold"
}

// Split values into tens and ones
void Split(int16_t value, int16_t &value_tens, int16_t &value_ones) {
  value_tens = value / 10; // Splits tens from the value 
  value_ones = value % 10; // Splits ones from the value
  return;
}

// Convert from decimal to a single integer "binary" format
int16_t ToBinary(int16_t n) {
  int16_t binary_number = 0, remainder, i = 1;
  while (n != 0)  {
    remainder = n % 2;
    n /= 2;
    binary_number += remainder * i;
    i *= 10;
  }
  return binary_number;
}
