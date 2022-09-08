/*
  MIT License

  Copyright (c) 2022 Delta Z Technical Services, LLC, Austin, TX.

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
  SOFTWARE.
*/

#include <Arduino.h>
#include <avr/wdt.h>
#include <Wire.h>
#include <TM1637Display.h>

#define HELP_STRINGS 1

#include "command-processor.h"
#include "shot-clock.h"
#include "shot-clock-commands.h"

extern char output_buf[];
extern uint8_t g_state;
extern int32_t g_clock_millis;
extern int32_t g_custom_reset_millis;
extern bool g_clock_is_running;
extern bool g_horn_is_on;

extern int32_t g_horn_timer_millis;
extern uint32_t g_uptime_seconds;
extern uint8_t g_horn_tenths;
extern int8_t g_brightness;
extern bool g_radio_ok;
extern int8_t g_radio_mode;
extern int8_t g_radio_channel;
extern struct display_info g_front_display;
extern struct display_info g_rear_display;
extern union color g_color;
extern uint8_t g_color_mode;
extern uint8_t g_inputs;
extern uint8_t g_button_pressed_events;
extern uint8_t g_button_released_events;

extern bool g_remote_clock_is_running;
extern uint8_t g_radio_signal_strength;

extern bool g_debug;

/* The first two characters of the message display on the front display (LEDs) of the clock,
   the last four on the rear display (TM1637) */

const char g_messages[][6] =
  {
   {MAJOR_VERSION, MINOR_VERSION, ' ', MAJOR_VERSION, '.', MINOR_VERSION},  // MESSAGE_VERSION
   {'H', 'i', 'H', 'E', 'L', 'O'}, // MESSAGE_HELLO
   {'O', 'F', 'r', 'O', 'F', 'F'}, // MESSAGE_RADIO_MODE_OFF
   {'b', 'r', 'b', 'r', 'o', 'A'}, // MESSAGE_RADIO_MODE_BROADCAST
   {'L', 'i', 'L', 'i', 'S', 't'}, // MESSAGE_RADIO_MODE_LISTEN
   {'H', 'o', 'H', 'o', 'r', 'n'}, // MESSAGE_SETTING_HORN
   {'C', 'o', 'C', 'o', 'l', 'o'}, // MESSAGE_SETTING_COLOR_MODE
   {'r', 'A', 'r', 'A', 'd', 'i'}, // MESSAGE_SETTING_RADIO_MODE
   {'C', 'h', 'C', 'h', 'A', 'n'}, // MESSAGE_SETTING_RADIO_CHANNEL
   {'S', 't', 'S', 't', 'r', 'E'}, // MESSAGE_SETTING_RADIO_STRENGTH
   {'C', 's', 'C', 'u', 'S', 't'}, // MESSAGE_COLOR_MODE_CUSTOM
   {'U', 'U', 'U', 'U', 'h', 't'}, // MESSAGE_COLOR_MODE_WHITE
   {'r', 'd', 'r', 'E', 'd', ' '}, // MESSAGE_COLOR_MODE_RED
   {'g', 'n', 'g', 'r', 'n', ' '}, // MESSAGE_COLOR_MODE_GREEN
   {'b', 'l', 'b', 'l', 'u', 'E'}, // MESSAGE_COLOR_MODE_BLUE
   {'F', 'd', 'F', 'A', 'd', 'E'}, // MESSAGE_COLOR_MODE_COLOR_FADE
   {'r', 'b', 'r', 'A', 'i', 'n'}, // MESSAGE_COLOR_MODE_RAINBOW
   {'S', 'A', 'S', 'A', 'U', 'E'}  // MESSAGE_SAVE
  };



/*
  Notation: following Forth documentation standards, "(n -- c1 c2)" means that the command expects
  to find a 16-bit number on the data stack, removes it, and leaves two 8-bit ascii-encoded characters
  on the stack.
*/

COMMAND_STRINGS(scan_i2c, "scan", "Scan the I2C bus and print out the results")
// const char command_name_search[] PROGMEM = "scan"; 
// const char command_help_search[] PROGMEM = "Scan the I2C bus and print out the results";
COMMAND_STRINGS(temp, "temp?", "( -- n) Read the temperature and place on the stack")
COMMAND_STRINGS(two_digits, "2digits", "(n -- c1 c2 ) put last two ASCII digits of n on the stack");
COMMAND_STRINGS(show_number, "show", "(n -- ) show last two digits of n on the display");
COMMAND_STRINGS(show_number_transitory, "shows", "(n -- ) put n in transitory display buffer and show for 1 second");
COMMAND_STRINGS(show_front, "showf", "(left right -- ) show the two ascii digits from the stack on the front display");
COMMAND_STRINGS(show_front_transitory, "showfs", "(left right -- ) put chars in transitory display buffer and show for 1 second");
COMMAND_STRINGS(show_rear, "showr", "(c0 c1 c2 c3 -- ) show the four ascii digits from the stack on the rear display");
COMMAND_STRINGS(show_rear_transitory, "showrs", "(left right -- ) put chars in transitory display buffer and show for 1 second");
COMMAND_STRINGS(show_time, "update", "update clock time on the LED display");
COMMAND_STRINGS(increase_time, "time+", "increase clock time by 1 second");
COMMAND_STRINGS(decrease_time, "time-", "decrease clock time by 1 second");
COMMAND_STRINGS(set_clock, "time!", "( n -- ) set clock seconds and update the LED display");
COMMAND_STRINGS(reset_30, "reset", "(left right -- ) reset the timer to 30 seconds");
COMMAND_STRINGS(reset_custom, "resetc", "(left right -- ) reset the timer to custom seconds");
COMMAND_STRINGS(increase_custom_reset_clock, "reset+", "(left right -- ) increase the custom reset time by 1 second");
COMMAND_STRINGS(decrease_custom_reset_clock, "reset-", "(left right -- ) decrease the custom reset time by 1 second");
COMMAND_STRINGS(start_clock, "start", "start clock");
COMMAND_STRINGS(stop_clock, "stop", "stop clock");
COMMAND_STRINGS(horn, "horn", "(n -- ) sound horn for n tenths of a second");
COMMAND_STRINGS(beep, "beep", "sound horn for set number of tenths of a second");
COMMAND_STRINGS(brightness_increase, "brightness+", "increase brightness");
COMMAND_STRINGS(brightness_decrease, "brightness-", "decrease brightness");
COMMAND_STRINGS(uptime, "uptime", "( -- n) put uptime in seconds on the stack")
COMMAND_STRINGS(hms, "hms", "");
COMMAND_STRINGS(settings_load, "settings", "load and print saved settings ");
COMMAND_STRINGS(settings_save, "save", "save settings");
COMMAND_STRINGS(reset_settings, "factory", "reset saved settings to factory values");
COMMAND_STRINGS(color, "color", "print out the color on the stack");
COMMAND_STRINGS(color_get, "color?", "(-- w r g b) put the current color on the stack");
COMMAND_STRINGS(color_set, "color!", "(w r g b --) set the current color");
COMMAND_STRINGS(color_mode_get, "colormode?", "( -- mode) Put the current color mode on the stack");
COMMAND_STRINGS(color_mode_set, "colormode!", "(mode -- ) Set the current color mode (mode=0-5)"); 
COMMAND_STRINGS(state, "state", "print the current state of the clock");
COMMAND_STRINGS(inputs, "inputs", "print the inputs and transitions");
COMMAND_STRINGS(radio_off, "roff", "turn off radio");
COMMAND_STRINGS(radio_broadcast, "broadcast", "broadcast current clock time and state on current channel");
COMMAND_STRINGS(radio_listen, "listen", "listen for radio broadcasts on current channel and update display");
COMMAND_STRINGS(radio_signal_test, "signal", "signal strength test: send 99 packets");
COMMAND_STRINGS(radio, "radio", "show radio parameters and update physical radio with them");


VARIABLE_STRINGS(clock, "clock", "millis on the clock (double)");
// expands to
// const char variable_name_clock[] PROGMEM = "clock"; 
// const char variable_help_clock[] PROGMEM = "millis on the clock (double)";
VARIABLE_STRINGS(horntenths, "horntenths", "duration the horn should sound, in tenths of a second (byte)"); 
VARIABLE_STRINGS(brightness, "brightness", "brightness of the leds, 1-5 (byte)"); 
VARIABLE_STRINGS(radio_mode, "radiomode", "current radio mode: 0 (off), 1 (broadcast), 2 (listen)");
VARIABLE_STRINGS(radio_channel, "radiochannel", "current radio channel (0-15)");


const struct dictionary_entry g_shot_clock_dictionary[] PROGMEM =
  {
   // expansion example:
   // {command_name_scan, TYPE_COMMAND, { .command = command_scan_i2c }},
   DICT_COMMAND_ENTRY(scan_i2c),
   DICT_COMMAND_ENTRY(show_front),
   DICT_COMMAND_ENTRY(show_front_transitory),
   DICT_COMMAND_ENTRY(show_rear),
   DICT_COMMAND_ENTRY(show_rear_transitory),
   DICT_COMMAND_ENTRY(two_digits),
   DICT_COMMAND_ENTRY(show_number),
   DICT_COMMAND_ENTRY(show_number_transitory),
   DICT_COMMAND_ENTRY(show_time),
   DICT_COMMAND_ENTRY(set_clock),
   DICT_COMMAND_ENTRY(increase_time),
   DICT_COMMAND_ENTRY(decrease_time),
   DICT_COMMAND_ENTRY(reset_30),
   DICT_COMMAND_ENTRY(reset_custom),
   DICT_COMMAND_ENTRY(increase_custom_reset_clock),
   DICT_COMMAND_ENTRY(decrease_custom_reset_clock),
   DICT_COMMAND_ENTRY(start_clock),
   DICT_COMMAND_ENTRY(stop_clock),
   DICT_COMMAND_ENTRY(horn),
   DICT_COMMAND_ENTRY(beep),
   DICT_COMMAND_ENTRY(brightness_increase),
   DICT_COMMAND_ENTRY(brightness_decrease),
   DICT_COMMAND_ENTRY(hms),
   DICT_COMMAND_ENTRY(uptime),
   DICT_COMMAND_ENTRY(settings_load),
   DICT_COMMAND_ENTRY(settings_save),
   DICT_COMMAND_ENTRY(reset_settings),
   DICT_COMMAND_ENTRY(color),
   DICT_COMMAND_ENTRY(color_get),
   DICT_COMMAND_ENTRY(color_set),
   DICT_COMMAND_ENTRY(color_mode_get),
   DICT_COMMAND_ENTRY(color_mode_set),
   DICT_COMMAND_ENTRY(state),
   DICT_COMMAND_ENTRY(inputs),
   DICT_COMMAND_ENTRY(radio_off),
   DICT_COMMAND_ENTRY(radio_broadcast),
   DICT_COMMAND_ENTRY(radio_listen),
   DICT_COMMAND_ENTRY(radio_signal_test),
   DICT_COMMAND_ENTRY(radio),
   DICT_DOUBLE_VARIABLE_ENTRY(clock, g_clock_millis),
   // above expands to
   // {variable_name_clock, TYPE_DVALUE, { .dvalue = &g_clock_millis }},
   DICT_CHAR_VARIABLE_ENTRY(horntenths, g_horn_tenths),
   DICT_CHAR_VARIABLE_ENTRY(brightness, g_brightness),
   DICT_CHAR_VARIABLE_ENTRY(radio_mode, g_radio_mode),
   DICT_CHAR_VARIABLE_ENTRY(radio_channel, g_radio_channel),
   {NULL, TYPE_END_OF_DICT, NULL} // end-of-dictionary sentinel
  };


const struct help_entry g_shot_clock_help[] PROGMEM =
  {
   //{command_name_scan, command_help_scan},
   HELP_COMMAND_ENTRY(scan_i2c),
   HELP_COMMAND_ENTRY(temp),
   HELP_COMMAND_ENTRY(show_front),
   HELP_COMMAND_ENTRY(show_front_transitory),
   HELP_COMMAND_ENTRY(show_rear),
   HELP_COMMAND_ENTRY(show_rear_transitory),
   HELP_COMMAND_ENTRY(two_digits),
   HELP_COMMAND_ENTRY(show_number),
   HELP_COMMAND_ENTRY(show_number_transitory),
   HELP_COMMAND_ENTRY(show_time),
   HELP_COMMAND_ENTRY(set_clock),
   HELP_COMMAND_ENTRY(increase_time),
   HELP_COMMAND_ENTRY(decrease_time),
   HELP_COMMAND_ENTRY(reset_30),
   HELP_COMMAND_ENTRY(reset_custom),
   HELP_COMMAND_ENTRY(increase_custom_reset_clock),
   HELP_COMMAND_ENTRY(decrease_custom_reset_clock),
   HELP_COMMAND_ENTRY(start_clock),
   HELP_COMMAND_ENTRY(stop_clock),
   HELP_COMMAND_ENTRY(horn),
   HELP_COMMAND_ENTRY(beep),
   HELP_COMMAND_ENTRY(brightness_increase),
   HELP_COMMAND_ENTRY(brightness_decrease),
   HELP_COMMAND_ENTRY(uptime),
   HELP_COMMAND_ENTRY(settings_load),
   HELP_COMMAND_ENTRY(settings_save),
   HELP_COMMAND_ENTRY(reset_settings),
   HELP_COMMAND_ENTRY(color),
   HELP_COMMAND_ENTRY(color_get),
   HELP_COMMAND_ENTRY(color_set),
   HELP_COMMAND_ENTRY(color_mode_get),
   HELP_COMMAND_ENTRY(color_mode_set),
   HELP_COMMAND_ENTRY(state),
   HELP_COMMAND_ENTRY(inputs),
   HELP_COMMAND_ENTRY(radio_off),
   HELP_COMMAND_ENTRY(radio_broadcast),
   HELP_COMMAND_ENTRY(radio_listen),
   HELP_COMMAND_ENTRY(radio_signal_test),
   HELP_COMMAND_ENTRY(radio),
   HELP_VARIABLE_ENTRY(clock),
   HELP_VARIABLE_ENTRY(horntenths),
   {NULL, NULL} // end-of-dictionary sentinel
  };

struct dictionary_entry *get_application_dictionary() {
  return g_shot_clock_dictionary;
}

struct help_entry *get_application_help() {
  return g_shot_clock_help;
}

void application_rc_printer(uint8_t rc) {
  switch (rc) {
  case (ERROR_TEMP_NOT_READ):
    Serial.println(F("SHOT CLOCK ERROR: Unable to read temperature"));
    break;
  case (ERROR_UNABLE_TO_DISPLAY_CHAR):
    Serial.println(F("SHOT CLOCK ERROR: Unable to display that character"));
    break;
  default:
    Serial.print(F("ERROR "));
    Serial.print(rc);
    Serial.println(F("???"));
    break;
  }
}

void command_scan_i2c() {
  int nDevices = 0;

  Serial.println(F("Scanning..."));

  for (byte address = 1; address < 127; ++address) {
    // The i2c_scanner uses the return value of
    // the Write.endTransmisstion to see if
    // a device did acknowledge to the address.
    Wire.beginTransmission(address);
    byte error = Wire.endTransmission();

    if (error == 0) {
      Serial.print(F("I2C device found at address 0x"));
      if (address < 16) {
        Serial.print(F("0"));
      }
      Serial.print(address, HEX);

      ++nDevices;
    } else if (error == 4) {
      Serial.print(F("Unknown error at address 0x"));
      if (address < 16) {
        Serial.print(F("0"));
      }
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0) {
    Serial.println(F("No I2C devices found"));
  }
  Serial.println();
}

void command_read_temperature() {

  Wire.beginTransmission(TEMP_SENSOR_I2C_ADDRESS);

  Wire.write(0); // request temperature in a byte

  Wire.endTransmission();

  Wire.requestFrom(TEMP_SENSOR_I2C_ADDRESS, 1);

  // Serial.println(F("DEBUG: requesting temperature"));
  
  if (Wire.available()) {
    int16_t celsius = Wire.read();
    int fahr = round(celsius*9.0/5.0+32.0);

    // maybe have a celsius/fahr setting some day
    push_single(fahr);

  } else {
    // we have to push something to indicate an error
    push_single(-40);
  }
}

void pop_into_display_buffer(struct display_info *display) {
  char *display_buf = display->use_primary_buffer ? 
    display->primary_buffer : display->transitory_buffer;

  for (int i=display->buffer_size - 1; i>=0; i--) {
    display_buf[i] = pop_single();
  }
  display->dirty = 1;
}

void command_show_front() {
  pop_into_display_buffer(&g_front_display);
}      

void command_show_rear() {
  pop_into_display_buffer(&g_rear_display);
}

void command_push_message_characters() {
  uint8_t message_id = pop_single();
  if(message_id > MESSAGE_MAX) {
    Serial.print(F("BAD MESSAGE ID: "));
    Serial.println(message_id);
    fatal_error(ERROR_BAD_MESSAGE_ID);
  }
  
  for(int i = 0; i < 6; i++) {
    push_single(g_messages[message_id][i]);
  }
}

void command_print_message() {
  int16_t message_id = pop_single();
  Serial.print(F("message_id="));
  Serial.print(message_id);
  
  push_single(message_id);
  command_push_message_characters();

  int16_t f1, f2, r1, r2, r3, r4;
  r4=pop_single();
  r3=pop_single();
  r2=pop_single();
  r1=pop_single();
  f2=pop_single();
  f1=pop_single();

  sprintf_P(output_buf, PSTR(" [%c%c]/[%c%c%c%c]"), (char)f1, (char)f2, (char)r1, (char)r2, (char)r3, (char)r4);
  Serial.println(output_buf);
}

void command_show_message() {
  //Serial.print(F("command_show_message(): "));
  //command_dup();
  //command_print_message();
  
  command_push_message_characters();
  command_show_rear();
  command_show_front();
}

void command_show_message_transitory() {
  //Serial.print(F("command_show_message_transitory(): "));
  //command_dup();
  //command_print_message();

  command_push_message_characters();
  command_show_rear_transitory();
  command_show_front_transitory();
}

void command_show_number_transitory() {
  switch_to_transitory_buffer();
  command_show_number();
}

void command_show_front_transitory() {
  switch_to_transitory_buffer();
  command_show_front();
}      

void command_show_rear_transitory() {
  switch_to_transitory_buffer();
  command_show_rear();
}      

void command_two_digits() {
  /* Pop a single number from the stack, push the two least significant digits back on the stack, separately. */
  int16_t n = pop_single();

  char right_digit = (n % 10) + '0';
  char left_digit = (n / 10) % 10 + '0';
  if(n < 10)
    left_digit = ' ';
  if(n < 0)
    right_digit = left_digit = '-';
    
  push_single(left_digit);
  push_single(right_digit);
}

void command_show_number() {
  /* The good thing here is that a leading zero, say on the temp,
     means we are in the hundreds.  No leading zero means in the singles. */
  command_two_digits();
  int16_t n0 = pop_single();
  int16_t n1 = pop_single();
  push_single(n1);
  push_single(n0);
  command_show_front();
  push_single(' ');
  push_single(' ');
  push_single(n1);
  push_single(n0);
  command_show_rear();
}

void command_show_time() {

  // 30000 to 29001 should display 30
  // 29000 to 28001 should display 29
  // ..
  // 2000 to 1001 should display 2
  // 1000 to 1 would display 1
  // but
  // 1000 to 901 should display 1
  // 900 to 801 should display .9
  // ..
  // 200 to 101 should display .2
  // 100 to 1 should display .1
  // 0 should display 0

  char display_time[10];

  /*
    sprintf_P(s, PSTR("%ld "), g_clock_millis);
    Serial.print(F("display_clock_time(): g_clock_millis="));
    Serial.println(s);
  */

  if (g_clock_millis > 900) {
    sprintf_P(display_time, PSTR("  %2d"), ((g_clock_millis - 1) / 1000) + 1); 
  } else if (g_clock_millis > 0) {
    sprintf_P(display_time, PSTR("  .%1d"), ((g_clock_millis - 1) / 100) + 1); 
  } else {
    sprintf_P(display_time, PSTR("   0"));
  }

  if(compare_active_display_buffer(&g_front_display, &display_time[2]) != 0) {
    push_single(display_time[2]);
    push_single(display_time[3]);
    command_show_front();
  }

  if(compare_active_display_buffer(&g_rear_display, display_time) != 0) {
    push_single(display_time[0]);
    push_single(display_time[1]);
    push_single(display_time[2]);
    push_single(display_time[3]);
    command_show_rear();
  }

  send_radio_command_show_time_if_necessary();
}

void command_set_clock() {
  int16_t seconds = pop_single();
  uint8_t short_seconds = seconds;
  wrap_range(&short_seconds, 0, 99);
  g_clock_millis = ((uint32_t)short_seconds) * 1000L;
  command_show_time();
}

void command_increase_time() {
  /* increase current time, max is 99 seconds */
  g_clock_millis = min(g_clock_millis + 1000, 99000);
  command_show_time();
}

void command_decrease_time() {
  /* decrease current time, min is 0 */
  g_clock_millis = g_clock_millis - 1000;
  if(g_clock_millis < 0) {
    g_clock_millis = 0;
  }
  command_show_time();
}

void command_reset_30() {
  g_clock_millis = 30000;
  command_show_time();
  Serial.print(F("Reset clock millis to "));
  Serial.println(g_clock_millis);
}

void command_reset_custom() {
  g_clock_millis = g_custom_reset_millis;
  command_show_time();
  Serial.print(F("Custom reset clock millis to "));
  Serial.println(g_clock_millis);
}

void command_increase_custom_reset_clock() {
  /* increase custom setting by 1 second, max is 99 seconds */
  g_custom_reset_millis = min(g_custom_reset_millis + 1000, 99000);
  Serial.print(F("Increased custom reset millis to "));
  Serial.println(g_custom_reset_millis);
  command_reset_custom();
}

void command_decrease_custom_reset_clock() {
  /* decrease custom setting by 1 second, minimum is 1 second */
  g_custom_reset_millis = max(g_custom_reset_millis - 1000, 1000);
  Serial.print(F("Decreased custom reset millis to "));
  Serial.println(g_custom_reset_millis);
  command_reset_custom();
}

void command_start_clock() {
  if (g_clock_millis > 0) {
    state_running();
  } else {
    command_reset_30();
    state_running();
  }
}

void command_stop_clock() {
  state_stopped();
}

void command_horn() {
  int16_t tenths = pop_single();
  g_horn_timer_millis = tenths * 100;
  digitalWrite(PIN_HORN_RELAY, HIGH);
  g_horn_is_on = true;
}

void command_beep() {
  // don't activate the relay if we don't need to - it clicks
  if(g_horn_tenths > 0) {
    push_single(g_horn_tenths);
    command_horn();
  }
  send_radio_command(RADIO_COMMAND_BEEP);
}

void command_horn_set() {
  int16_t tenths = pop_single();
  g_horn_tenths = tenths;
  wrap_range(&g_horn_tenths, 0, MAX_HORN_TENTHS);
  bool changes = save_settings();
  
  Serial.print(F("Set horn to "));
  Serial.print(g_horn_tenths);
  Serial.print(F(" tenths of a second"));
}

void command_horn_get() {
  Serial.print(F("Horn duration is "));
  Serial.print(g_horn_tenths);
  Serial.print(F(" tenths of a second"));
}

void command_horn_increase() {
  push_single(g_horn_tenths);
  command_1_plus();
  command_horn_set();
}

void command_horn_decrease() {
  push_single(g_horn_tenths);
  command_1_minus();
  command_horn_set();
}

void command_hms() {
  int16_t h,m,s;
  s = pop_single();

  h = s / 3600;
  m = s/ 60 - (h* 60);
  s = s % 60;
  sprintf_P(output_buf, PSTR("%d hours %d minutes %d seconds"), h,m,s);
  Serial.println(output_buf);
}

void command_uptime() {
  push_single(g_uptime_seconds);
}

void command_settings_load() {
  load_settings();
  Serial.println(F("Settings"));
  Serial.println(F("--------"));
  Serial.print(F("Brightness: "));
  Serial.println(g_brightness);
  Serial.print(F("Horn duration in tenths: "));
  Serial.println(g_horn_tenths);
  Serial.print(F("Radio mode: "));
  Serial.println(g_radio_mode);
  Serial.print(F("Radio channel: "));
  Serial.println(g_radio_channel);
}

void command_settings_save() {
  bool changes = save_settings();
  if(changes) {
    Serial.println(F("Changes saved."));
  } else {
    Serial.println(F("No changes."));
  }
}

void command_reset_settings() {
  reset_settings();
  Serial.println(F("Reset settings to factory defaults.\n"));
  command_settings_load();
}

void command_brightness_set() {
  int16_t b = pop_single();
  g_brightness = b;
  wrap_range(&g_brightness, 1, 5);
  set_led_brightness();
  bool changes = save_settings();
  Serial.print(F("Set brightness to "));
  Serial.println(g_brightness);
}

void command_brightness_increase() {
  push_single(g_brightness);
  command_1_plus();
  command_brightness_set();
}

void command_brightness_decrease() {
  push_single(g_brightness);
  command_1_minus();
  command_brightness_set();
}

void command_color_get() {
  // print out the instantaneous color
  push_single(g_color.parts.red);
  push_single(g_color.parts.green);
  push_single(g_color.parts.blue);
  push_single(g_color.parts.white);
}

void command_color() {
  union color c;
  int16_t r,g,b,w;
  
  w = pop_single();
  // c.parts.white = (uint8_t)w;
  
  c.parts.white = 0;
  
  b = pop_single();
  c.parts.blue = (uint8_t)b;
  
  g = pop_single();
  c.parts.green = (uint8_t)g;
      
  r = pop_single();
  c.parts.red = (uint8_t)r;

  sprintf_P(output_buf, PSTR(" red=%d green=%d blue=%d white=%d  "),
	    c.parts.red, c.parts.green, c.parts.blue, c.parts.white);
  Serial.println(output_buf);
  /*
    Serial.print(F("red="));
    Serial.print(c.parts.red);
    Serial.print(F(" green="));
    Serial.print(c.parts.green);
    Serial.print(F(" blue="));
    Serial.print(c.parts.blue);
    Serial.print(F(" white="));
    Serial.print(c.parts.white);
  */
  sprintf_P(output_buf, PSTR(" wrgb=0x%08lx "), c.wrgb);
  Serial.println(output_buf);
}

void command_color_set() {
  int16_t w,r,g,b;
  w = 0; // the Adafruit_NeoPixel library ignores white.

  b = pop_single();
  g = pop_single();
  r = pop_single();

  g_color.parts.red = r;
  g_color.parts.green = g;
  g_color.parts.blue = b;
  g_color.parts.white = w;
  g_color_mode = COLOR_MODE_NONE;
  display_dirty(&g_front_display);
}

void command_color_mode_set() {
  int16_t mode = pop_single();
  g_color_mode = mode;
  wrap_range(&g_color_mode, COLOR_MODE_NONE, MAX_COLOR_MODE);
  display_dirty(&g_front_display);
}

void command_color_mode_get() {
  push_single(g_color_mode);
}

void print_display_buffers(struct display_info *display) {
  int i;
  Serial.print(F("["));
  for(i = 0; i < display->buffer_size; i++) {
    Serial.print((char)display->primary_buffer[i]);
  }

  Serial.print(F("] ["));
  for(i = 0; i < display->buffer_size; i++) {
    Serial.print((char)display->transitory_buffer[i]);
  }
  Serial.print(F("]"));
}

void command_state() {
  print_radio_mode();
  
  sprintf_P(output_buf, PSTR("State: %d (g_clock_is_running=%d) "), g_state, g_clock_is_running);
  Serial.print(output_buf);

  sprintf_P(output_buf, PSTR("g_horn_is_on=%d "), g_horn_is_on);
  Serial.print(output_buf);
  
  sprintf_P(output_buf, PSTR("Clock millis: %d "), g_clock_millis);
  Serial.println(output_buf);

  Serial.print(F("Front buffers (primary, transitory): "));
  print_display_buffers(&g_front_display);
  Serial.println();

  Serial.print(F("Rear buffers (primary, transitory): "));
  print_display_buffers(&g_rear_display);
  Serial.println();
  
  sprintf_P(output_buf, PSTR("Use primary buffer: %d"), g_front_display.use_primary_buffer);
  Serial.println(output_buf);

  sprintf_P(output_buf, PSTR("Dirty: %d"), g_front_display.dirty);
  Serial.println(output_buf);

  sprintf_P(output_buf, PSTR("Millis until primary switch: %d"), g_front_display.transitory_timer_millis);
  Serial.println(output_buf);

  Serial.print(F("Brightness: "));
  Serial.print(g_brightness);

  command_color_get();
  command_color();
  Serial.print(F("Color mode: "));
  Serial.println(g_color_mode);
}

void command_inputs() {

  Serial.print(F("Inputs: "));
  sprintf_P(output_buf, PSTR(BYTE_TO_BINARY_PATTERN), BYTE_TO_BINARY_REVERSE(g_inputs));
  Serial.print(output_buf);
  print_buttons(g_inputs);
  Serial.println();

  Serial.print(F("Buttons pressed: "));
  sprintf_P(output_buf, PSTR(BYTE_TO_BINARY_PATTERN), BYTE_TO_BINARY_REVERSE(g_button_pressed_events));
  Serial.print(output_buf);
  print_buttons(g_button_pressed_events);
  Serial.println();

  Serial.print(F("Buttons released: "));
  sprintf_P(output_buf, PSTR(BYTE_TO_BINARY_PATTERN), BYTE_TO_BINARY_REVERSE(g_button_released_events));
  Serial.print(output_buf);
  print_buttons(g_button_released_events);
  Serial.println();
}

void command_radio_off() {
  g_radio_mode = RADIO_MODE_OFF;
  command_radio();
}

void command_radio_broadcast() {
  g_radio_mode = RADIO_MODE_BROADCAST;
  command_radio();
}

void command_radio_listen() {
  g_radio_mode = RADIO_MODE_LISTEN;
  command_radio();
}

void command_radio_signal_test() {
  if(prepare_radio_signal_test()) {
    // todo this in the settings, I need to stay in a state (keep calling) until I get a "finished" result.
    
    Serial.print(F("Signal test: "));
    while(send_test_packet()) {
    }
    Serial.print(F(" "));
    Serial.println(g_radio_signal_strength);
    complete_radio_signal_test();
  }
}

void command_radio() {
  if(!g_radio_ok) {
    Serial.println(F("Radio not OK."));
    return;
  }

  Serial.println();
  update_radio();
  print_radio_mode();
}

void command_version() {
  Serial.print(MAJOR_VERSION);
  Serial.print(F("."));
  Serial.println(MINOR_VERSION);
}

void command_debug() {
  if(g_debug) {
    g_debug = false;
    Serial.println(F("debug off"));
  } else {
    g_debug = true;
    Serial.println(F("debug on"));
  }
}
