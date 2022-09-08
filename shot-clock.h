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

#define MAJOR_VERSION '0'
#define MINOR_VERSION '3'

#define BUILD_TEST 0
#define HALT Serial.println(F("HALT")); while(1);

/*
| Pin   | Function                 | I/O    |
|-------+--------------------------+--------|
| D0/RX | Built-in Serial RX       |        |
| D1/TX | Built-in Serial TX       |        |
| D2    | Right LED string         | OUT    |
| D3    | UP button                | IN     |
| D4    | START_STOP button        | IN     |
| D5    | RESET_30 button          | IN     |
| D6    | RESET_20 button          | IN     |
| D7    | CE on radio              | OUT    |
| D8    | CSN on radio             | OUT    |
| D9    | HORN button              | IN     |
| D10   | Left LED string          | OUT    |
| D11   | MOSI (for radio)         |        |
| D12   | MISO (for radio)         |        |
| D13   | SCK (for radio)          |        |
| A0    | DOWN button              | IN     |
| A1    | HORN relay               | OUT    |
| A2    | TM1637 DIO               | IN/OUT |
| A3    | TM1637 CLK               | IN/OUT |
| A4    | SDA (for temp)           |        |
| A5    | SCL (for temp)           |        |
| A6    |                          |        | 328p analog input only
| A7    | CONFIG                   | IN     | 328p analog input only
| 3v3   | power for NRF24L01 radio |        |
| VIN   | +5V from buck converter  |        |
| GND   | GND from buck converter  |        |
*/

/*
 For SPI to work, pin 10 (SS) must be set to OUTPUT, even if not used for SPI.  If you change any
 pins, make sure to update the ISRs, and PCMSK's if needed.
*/
#define PIN_RIGHT_LED_STRING 2
#define PIN_LEFT_LED_STRING 10  
#define PIN_START_STOP_BUTTON 4
#define PIN_RESET_30_BUTTON 5
#define PIN_RESET_20_BUTTON 6
#define PIN_SETTINGS_BUTTON 9
#define PIN_UP_BUTTON 3
#define PIN_DOWN_BUTTON A0
#define PIN_HORN_RELAY A1
#define PIN_TM1637_DIO A2
#define PIN_TM1637_CLK A3
#define PIN_CONFIG A7
#define PIN_RADIO_CE 7
#define PIN_RADIO_CSN 8

#define STATE_UNINITIALIZED 0
#define STATE_INIT 1
#define STATE_STOPPED 2
#define STATE_RUNNING 3
#define STATE_SETTING 4

#define INPUT_START_STOP_BUTTON 1
#define INPUT_RESET_30_BUTTON 2
#define INPUT_RESET_20_BUTTON 4
#define INPUT_UP_BUTTON 8
#define INPUT_DOWN_BUTTON 16
#define INPUT_SETTINGS_BUTTON 32

// easier to think of these when working with the remote
// for alternative up/down buttons.
#define INPUT_A INPUT_START_STOP_BUTTON
#define INPUT_B INPUT_RESET_30_BUTTON
#define INPUT_C INPUT_RESET_20_BUTTON
#define INPUT_D INPUT_SETTINGS_BUTTON

#define INPUT_HISTORY_LENGTH 6 // record last 6 inputs on transition.

#define PIXELS_PER_SEGMENT 7 // 8 for the other clock
#define LED_COUNT 50
#define DISPLAY_TEMP_MILLIS 1000

/*
Segments:
   _____
  <__A__>
 /\     /\
|  |   |  |
|F |   |B |
|  |   |  |
 \/____ \/
  <__G__>
 /\     /\
|  |   |  |
|E |   |C |
|  |   |  |
 \/____ \/
  <__D__>
*/

#define SEGMENT_E 0
#define SEGMENT_D 1
#define SEGMENT_C 2
#define SEGMENT_B 3
#define SEGMENT_A 4
#define SEGMENT_F 5
#define SEGMENT_G 6

#define INIT_STATE_WELCOME 0
#define INIT_STATE_SHOW_VERSION 1
#define INIT_STATE_SHOW_RADIO 2
#define INIT_STATE_DONE 3

#define INIT_DISPLAY_DELAY 1000L

#define SETTING_STATE_HORN 0
#define SETTING_STATE_COLOR_MODE 1
#define SETTING_STATE_RADIO_MODE 2
#define SETTING_STATE_RADIO_CHANNEL 3
#define SETTING_STATE_RADIO_STRENGTH 4
#define MAX_SETTING_STATE SETTING_STATE_RADIO_STRENGTH
#define SETTING_SAVED 5

#define SETTING_TIMEOUT_MILLIS 2000L

#define EEPROM_BRIGHTNESS 0x00
#define EEPROM_HORN_TENTHS 0x01
#define EEPROM_RADIO_MODE 0x02
#define EEPROM_RADIO_CHANNEL 0x03

#define DEFAULT_HORN_TENTHS 13
#define MAX_HORN_TENTHS 30
#define DEFAULT_BRIGHTNESS 5
#define MAX_BRIGHTNESS 5

#define ANIMATION_REFRESH_INTERVAL_MILLIS 18
#define DEFAULT_REFRESH_INTERVAL_MILLIS   1000L
#define DEFAULT_TRANSITORY_DISPLAY_MILLIS 1000L

#define TEMP_SENSOR_I2C_ADDRESS 0x48

#define COLOR_MODE_NONE       0
#define COLOR_MODE_WHITE      1
#define COLOR_MODE_RED        2
#define COLOR_MODE_GREEN      3
#define COLOR_MODE_BLUE       4
#define COLOR_MODE_COLOR_FADE 5
#define COLOR_MODE_RAINBOW    6
#define MIN_COLOR_MODE        COLOR_MODE_NONE
#define MAX_COLOR_MODE        COLOR_MODE_RAINBOW

#define MAX_HORN_TENTHS 30

#define ERROR_TEMP_NOT_READ 101
#define ERROR_UNABLE_TO_DISPLAY_CHAR 102

#define BUTTON_PRESSED(btn) ((btn & g_button_pressed_events) != 0)
#define BUTTON_PRESSED_NO_MODS(btn) (((btn & g_button_pressed_events) != 0) && (((~btn) & g_inputs) == 0))
#define BUTTON_RELEASED(btn) ((btn & g_button_released_events) != 0)
#define BUTTON_DOWN(btn) ((btn & g_inputs) != 0)
#define MODS(btn) ((~btn) & g_inputs) // select the modifiers

#define RADIO_MODE_OFF 0
#define RADIO_MODE_BROADCAST 1
#define RADIO_MODE_LISTEN 2
#define MIN_RADIO_MODE RADIO_MODE_OFF
#define MAX_RADIO_MODE RADIO_MODE_LISTEN

#define MIN_RADIO_CHANNEL 0
#define MAX_RADIO_CHANNEL 15

#define RADIO_DELAY                    5  
#define RADIO_RETRIES                  15
// If these are two large, then the clock will stutter in broadcast mode if there
// is no corresponding clock listening, due to all the retries.
#define MAX_TRANSMISSION_RETRIES       1
#define MAX_TRANSMISSION_MILLIS        50
  
#define MESSAGE_VERSION                0
#define MESSAGE_HELLO                  1
#define MESSAGE_RADIO_MODE_OFF         2
#define MESSAGE_RADIO_MODE_BROADCAST   3
#define MESSAGE_RADIO_MODE_LISTEN      4
#define MESSAGE_SETTING_HORN           5
#define MESSAGE_SETTING_COLOR_MODE     6
#define MESSAGE_SETTING_RADIO_MODE     7
#define MESSAGE_SETTING_RADIO_CHANNEL  8
#define MESSAGE_SETTING_RADIO_STRENGTH 9
#define MESSAGE_COLOR_MODE_CUSTOM     10
#define MESSAGE_COLOR_MODE_WHITE      11
#define MESSAGE_COLOR_MODE_RED        12
#define MESSAGE_COLOR_MODE_GREEN      13
#define MESSAGE_COLOR_MODE_BLUE       14
#define MESSAGE_COLOR_MODE_COLOR_FADE 15
#define MESSAGE_COLOR_MODE_RAINBOW    16
#define MESSAGE_SAVE                  17
#define MESSAGE_MAX                   MESSAGE_SAVE

#define ERROR_BAD_MESSAGE_ID 100

#define RADIO_ADDRESS {'S','P','Q','R', 1}
#define RADIO_COMMAND_SHOW_TIME 1
#define RADIO_COMMAND_BEEP 2
#define RADIO_COMMAND_CLOCK_STARTED 3
#define RADIO_COMMAND_CLOCK_STOPPED 4
#define RADIO_COMMAND_SIGNAL_TEST 5
#define MAX_RADIO_COMMAND RADIO_COMMAND_SIGNAL_TEST

#define SEG_DP   0b10000000 // for the TM1637 decimal point segment
union color {
  uint32_t wrgb;
  struct {
    uint8_t blue, green, red, white;
  } parts;
};

#define FRONT_DISPLAY_BUFFER_SIZE 2
#define REAR_DISPLAY_BUFFER_SIZE 4

struct display_info {
  char *primary_buffer;
  char *transitory_buffer;
  unsigned int buffer_size;
  uint16_t use_primary_buffer : 1;
  uint16_t dirty : 1;
  uint16_t animated : 1;
  uint16_t requires_refresh : 1;
  int16_t transitory_timer_millis : 12; // timer to switch back to primary
  int16_t refresh_millis; // timer to refresh display
};

struct radio_message {
  uint8_t sender_serial_number;
  uint16_t message_serial_number;
  uint8_t command;  
  int32_t clock_millis;
  uint8_t clock_running;
  uint8_t checksum;
};
  
void state_stopped(void);
void state_running(void);
void load_settings(void);
bool save_settings(void);
void reset_settings(void);
void set_led_brightness(void);
void display_dirty(struct display_info *display);
void switch_to_primary_buffer(void);
void switch_to_transitory_buffer(void);
void set_display(struct display_info *display, char *contents);
void print_buttons(uint8_t buttons);
int compare_active_display_buffer(struct display_info *display, char *contents);
void update_radio(void);
bool send_radio_command(uint8_t command);
void wrap_range(int8_t *value, int8_t min, int8_t max);
void send_radio_command_show_time_if_necessary(void);
void print_radio_mode(void);
bool prepare_radio_signal_test(void);
void complete_radio_signal_test(void);
bool send_test_packet(void);
