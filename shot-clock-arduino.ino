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

#define SERIAL_DEBUG

#include <avr/wdt.h>
#include <EEPROM.h>
#include <Wire.h>
#include <Adafruit_NeoPixel.h>
#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <TM1637Display.h>
#include "shot-clock.h"
#include "command-processor.h"
#include "shot-clock-commands.h"

extern char output_buf[];

bool g_debug = false; // turn on in places start debug output after a certain event

int32_t g_clock_millis = 0;
int32_t g_last_clock_millis = 0;
bool g_clock_is_running = false;
bool g_remote_clock_is_running = false;

uint32_t g_last_loop_millis = 0;
int32_t g_custom_reset_millis = 20000;

bool g_auto_run = false;

uint8_t g_state = STATE_UNINITIALIZED;

/* EEPROM Saved settings */
uint8_t g_horn_tenths= DEFAULT_HORN_TENTHS; // save in EEPROM, 255 means never set
uint8_t g_brightness = DEFAULT_BRIGHTNESS; // 1-5
bool g_horn_is_on = false;

int32_t g_horn_timer_millis = 0; // how much time the horn has left.
uint32_t g_uptime_seconds = 0;
uint32_t g_last_uptime_millis = 0;

struct display_info g_front_display;
char g_front_display_primary_buffer[FRONT_DISPLAY_BUFFER_SIZE];
char g_front_display_transitory_buffer[FRONT_DISPLAY_BUFFER_SIZE];

struct display_info g_rear_display;
char g_rear_display_primary_buffer[REAR_DISPLAY_BUFFER_SIZE];
char g_rear_display_transitory_buffer[REAR_DISPLAY_BUFFER_SIZE];

Adafruit_NeoPixel g_right_digit(LED_COUNT, PIN_RIGHT_LED_STRING, NEO_RGB + NEO_KHZ800);
Adafruit_NeoPixel g_left_digit(LED_COUNT, PIN_LEFT_LED_STRING, NEO_RGB + NEO_KHZ800);
TM1637Display g_tm1637_display(PIN_TM1637_CLK, PIN_TM1637_DIO);

volatile uint8_t g_inputs_volatile = 0;
uint8_t g_inputs = 0;
uint8_t g_last_inputs = 0;
uint8_t g_button_pressed_events = 0;
uint8_t g_button_released_events = 0;

uint32_t g_state_timeout_millis = 0; // used in any state where we want a timeout to leave it.

// Current color setting.  Put in a union so it is easy to use with
// some of the Adafruit_NeoPixel color conversion utilities.
union color g_color;

uint8_t g_color_mode = COLOR_MODE_WHITE;

RF24 g_radio(PIN_RADIO_CE, PIN_RADIO_CSN);
uint8_t g_radio_channel = MIN_RADIO_CHANNEL;
bool g_radio_ok = false;
uint8_t g_radio_mode = RADIO_MODE_OFF;
uint8_t g_radio_signal_strength = 0;

struct radio_message g_radio_message;
uint16_t g_message_serial_number = 0;
/* 
  We lookup the segments in this table by scanning it.  Makes it easy to add more.
  No slower than a case statement.

  Segments:
   _____
  <__A__>
 /\     /\
 | |   | |
 |F|   |B|
 | |   | |
 \/____ \/
  <__G__>
 /\     /\
 | |   | |
 |E|   |C|
 | |   | |
 \/_____\/
  <__D__>

*/

const uint8_t char_to_segment_table[][2] =
  {      //XGFEDCBA
   {' ', 0b00000000},
   {'0', 0b00111111},
   {'1', 0b00000110},
   {'2', 0b01011011},
   {'3', 0b01001111},
   {'4', 0b01100110},
   {'5', 0b01101101},
   {'6', 0b01111101},
   {'7', 0b00000111},
   {'8', 0b01111111},
   {'9', 0b01101111},
   {'A', 0b01110111},
   {'b', 0b01111100},
   {'C', 0b00111001},
   {'d', 0b01011110},
   {'E', 0b01111001},
   {'F', 0b01110001},
   {'g', 0b01101111},
   {'H', 0b01110110},
   {'h', 0b01110100},
   {'i', 0b00010000},
   {'I', 0b00110000},
   {'j', 0b00001100},
   {'J', 0b00001110},
   {'L', 0b00111000},
   {'l', 0b00110000},
   {'n', 0b01010100},
   {'o', 0b01011100},
   {'O', 0b00111111},
   {'P', 0b01110011},
   {'r', 0b01010000},
   {'S', 0b01101101},
   {'t', 0b01111000},
   {'U', 0b00111110},
   {'u', 0b00011100},
   {'y', 0b01101110},
   {'Z', 0b01011011},
   {'.', 0b00000000},
   {'-', 0b01000000},
   {0,   0b01010011}  // sort of a question mark looking thing
  };

/* 
    If we change an input pin purpose in shot-clock.h, we also have to change the input mapping here
    in the ISRs.
*/

ISR (PCINT0_vect) {
  // one of pins D8 to D13 has changed
  if(PINB & bit(PINB1)) {
    g_inputs_volatile |= INPUT_SETTINGS_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_SETTINGS_BUTTON;
  }
}

ISR (PCINT1_vect) {
  // one of pins A0 to A5 or RST has changed
  if(PINC & bit(PINC0)) {
    g_inputs_volatile |= INPUT_DOWN_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_DOWN_BUTTON;
  }
}

ISR (PCINT2_vect) {
  // one of pins D0 to D7 has changed
  if(PIND & bit(PIND3)) {
    g_inputs_volatile |= INPUT_UP_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_UP_BUTTON;
  }

  if(PIND & bit(PIND4)) {
    g_inputs_volatile |= INPUT_START_STOP_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_START_STOP_BUTTON;
  }

  if(PIND & bit(PIND5)) {
    g_inputs_volatile |= INPUT_RESET_30_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_RESET_30_BUTTON;
  }

  if(PIND & bit(PIND6)) {
    g_inputs_volatile |= INPUT_RESET_20_BUTTON;
  } else {
    g_inputs_volatile &= ~INPUT_RESET_20_BUTTON;
  }
}

void setup_watchdog() {
  cli();
  wdt_reset();

  /* 
     WDTCSR configuration bits
     --------------------------
     WDIE = 0: interrupt enable
     WDE = 1 : reset enable

     | WDP3 | WDP2 | WDP1 | WDP0 | Time-out(ms) |
     |------+------+------+------+--------------|
     |    0 |    0 |    0 |    0 |           16 |
     |    0 |    0 |    0 |    1 |           32 |
     |    0 |    0 |    1 |    0 |           64 |
     |    0 |    0 |    1 |    1 |          125 |
     |    0 |    1 |    0 |    0 |          250 |
     |    0 |    1 |    0 |    1 |          500 |
     |    0 |    1 |    1 |    0 |         1000 |
     |    0 |    1 |    1 |    1 |         2000 |
     |    1 |    0 |    0 |    0 |         4000 |
     |    1 |    0 |    0 |    1 |         8000 |


     At 57600 baud, (/ 57600 8) = 7200 chars per second, an 80 character message
     transmits in (/ 80 7200.0) = .011 seconds, or call it 12 ms.
     So we should be safe.
     Also, measure how long it takes to get an I2C message, or a radio message.

     For 250ms timeout:
     WDP3 = 0
     WDP2 = 1
     WDP1 = 0
     WDP0 = 0
   */

  // Enter Watchdog Configuration mode
  WDTCSR |= (1<<WDCE) | (1<<WDE);

  // Set Watchdog settings: enable reset after 250ms.
  WDTCSR = (0<<WDIE) | (1<<WDE) | (0<<WDP3) | (1<<WDP2) | (0<<WDP1) | (0<<WDP0);
  sei();
}

// uint8_t saved_MCUSR = MCUSR;
void radio_broadcast() {
  g_radio.stopListening();                // put radio in TX mode
}

void radio_listen() {
  g_radio.startListening();
}

void radio_off() {
  g_radio.stopListening();                // put radio in TX mode, but don't send anything
}
  
void setup_radio() {
  g_radio_ok = g_radio.begin();
  if(g_radio_ok) {
    g_radio.setPALevel(RF24_PA_MAX);
    g_radio.setDataRate( RF24_250KBPS ); // = 31250 chars/second = .032 ms per char. 100 chars in 3.2ms.
    // Reliability seems to be drastically affected if the Arduino serial cable is plugged in.
    g_radio.setRetries(RADIO_DELAY, RADIO_RETRIES); // delay, count

    byte address[5] = RADIO_ADDRESS;
    g_radio.openWritingPipe(address);
    g_radio.openReadingPipe(1, address);

    Serial.println(F("Radio successfully initialized."));
  } else {
    Serial.println(F("FAILURE initializing radio."));
  }
}

void setup() {

  Wire.begin();

  pinMode(PIN_RIGHT_LED_STRING, OUTPUT);
  pinMode(PIN_LEFT_LED_STRING, OUTPUT);

  pinMode(PIN_START_STOP_BUTTON, INPUT);
  pinMode(PIN_RESET_30_BUTTON, INPUT);
  pinMode(PIN_RESET_20_BUTTON, INPUT);
  pinMode(PIN_UP_BUTTON, INPUT);
  pinMode(PIN_DOWN_BUTTON, INPUT);
  pinMode(PIN_SETTINGS_BUTTON, INPUT);

  pinMode(PIN_HORN_RELAY, OUTPUT);

  pinMode(PIN_CONFIG, INPUT_PULLUP);
  
  /* 
  Enable the pin change interrupt masks for the buttons

  | Atmega pin | Arduino pin | Function | My clock pin      | Interrupt | Pin | PCMSK  | PORT  | ISR         |
  |------------+-------------+----------+-------------------+-----------+-----+--------+-------+-------------|
  | PB0        | D8          |          | CSN (radio)       | PCINT0    | PB0 | PCMSK0 | PORTB | PCINT0_vect |
  | PB1        | D9          |          | SETTINGS_BUTTON   | PCINT1    | PB1 |        |       |             |
  | PB2        | D10         |          | LEFT_LED_STRING   | PCINT2    | PB2 |        |       |             |
  | PB3        | D11         | MOSI     | MOSI              | PCINT3    | PB3 |        |       |             |
  | PB4        | D12         | MISO     | MISO              | PCINT4    | PB4 |        |       |             |
  | PB5        | D13         | SCK      | SCK               | PCINT5    | PB5 |        |       |             |
  |------------+-------------+----------+-------------------+-----------+-----+--------+-------+-------------|
  | PC0        | A0          |          | DOWN_BUTTON       | PCINT8    | PC0 | PCMSK1 | PORTC | PCINT1_vect |
  | PC1        | A1          |          | HORN_RELAY        | PCINT9    | PC1 |        |       |             |
  | PC2        | A2          |          |                   | PCINT10   | PC2 |        |       |             |
  | PC3        | A3          |          |                   | PCINT11   | PC3 |        |       |             |
  | PC4        | A4          | SDA      | SDA               | PCINT12   | PC4 |        |       |             |
  | PC5        | A5          | SCL      | SCL               | PCINT13   | PC5 |        |       |             |
  | PC6        | RST         |          |                   | PCINT14   | PC6 |        |       |             |
  |------------+-------------+----------+-------------------+-----------+-----+--------+-------+-------------|
  | ADC6       | A6          |          |                   | -         | -   | -      | -     |             |
  | ADC7       | A7          |          |                   | -         | -   | -      | -     |             |
  |------------+-------------+----------+-------------------+-----------+-----+--------+-------+-------------|
  | PD0        | D0          | TX       |                   | PCINT16   | PD0 | PCMSK2 | PORTD | PCINT2_vect |
  | PD1        | D1          | RX       |                   | PCINT17   | PD1 |        |       |             |
  | PD2        | D2          | INT0     | RIGHT_LED_STRING  | PCINT18   | PD2 |        |       |             |
  | PD3        | D3          | INT1     | UP_BUTTON         | PCINT19   | PD3 |        |       |             |
  | PD4        | D4          |          | START_STOP_BUTTON | PCINT20   | PD4 |        |       |             |
  | PD5        | D5          |          | RESET_30_BUTTON   | PCINT21   | PD5 |        |       |             |
  | PD6        | D6          |          | RESET_20_BUTTON   | PCINT22   | PD6 |        |       |             |
  | PD7        | D7          |          | CE (radio)        | PCINT23   | PD7 |        |       |             |
  |------------+-------------+----------+-------------------+-----------+-----+--------+-------+-------------|
  */
  PCMSK0 |= bit (PCINT1);  // SETTINGS_BUTTON, PB1, D9
  PCMSK1 |= bit (PCINT8);  // DOWN_BUTTON, PC0, A0
  PCMSK2 |= bit (PCINT19);  // UP_BUTTON, PD3, D3
  PCMSK2 |= bit (PCINT20);  // START_STOP_BUTTON, PD4, D4
  PCMSK2 |= bit (PCINT21);  // RESET_30_BUTTON, PD5, D5
  PCMSK2 |= bit (PCINT22);  // RESET_20_BUTTON, PD6, D6

  // Enable the port change interrupts for all ports
  PCICR |= bit(PCIE2) | bit(PCIE1) | bit(PCIE0);

  Serial.begin(115200);
  Serial.println();
  Serial.println(F("*** Shot clock initializing. ***"));

  g_right_digit.begin();
  g_left_digit.begin();
  g_right_digit.clear();
  g_right_digit.show();
  g_left_digit.clear();
  g_left_digit.show();
  delay(125);
    
  init_displays();

  setup_radio();

  load_settings();

  // POST may have delays in it, which would make the watchdog upset, so we do this last
  setup_watchdog();

  state_init();
}

void clear_display(struct display_info *display) {
  memset(display->primary_buffer, ' ', display->buffer_size);
  memset(display->transitory_buffer, ' ', display->buffer_size);
}

void init_displays() {
  g_front_display.buffer_size = FRONT_DISPLAY_BUFFER_SIZE;
  g_front_display.primary_buffer = g_front_display_primary_buffer;
  g_front_display.transitory_buffer = g_front_display_transitory_buffer;
  g_front_display.use_primary_buffer = 1;
  g_front_display.animated = 0;
  g_front_display.transitory_timer_millis = 0;
  g_front_display.refresh_millis = 0;
  g_front_display.requires_refresh = 1;
  clear_display(&g_front_display);

  g_rear_display.buffer_size = REAR_DISPLAY_BUFFER_SIZE;
  g_rear_display.primary_buffer = g_rear_display_primary_buffer;
  g_rear_display.transitory_buffer = g_rear_display_transitory_buffer;
  g_rear_display.use_primary_buffer = 1;
  g_front_display.animated = 0;
  g_rear_display.refresh_millis = 0;
  g_rear_display.requires_refresh = 0;
  clear_display(&g_rear_display);

  g_color_mode = COLOR_MODE_WHITE;

  g_tm1637_display.setBrightness(0x0f);
  g_tm1637_display.clear();

  displays_dirty();

  update_display(&g_front_display);
  update_display(&g_rear_display);
}

void displays_dirty() {
  display_dirty(&g_front_display);
  display_dirty(&g_rear_display);
}

void display_dirty(struct display_info *display) {
  display->dirty = 1;
}

void reset_settings() {
  g_brightness = DEFAULT_BRIGHTNESS;
  g_horn_tenths = DEFAULT_HORN_TENTHS;
  bool changes = save_settings();
}

void update_radio() {
  // The radio channel is between 0-125.  We have 0-15 as our allowed settings.

  Serial.print(F("Radio channel is "));
  Serial.println(g_radio_channel);

  uint8_t channel = g_radio_channel * 8;

  Serial.print(F("Setting radio frequency to "));
  Serial.print(2000L + channel);
  Serial.println(F(" MHz"));
  
  g_radio.setChannel(channel);

  Serial.print(F("Radio frequency is now "));
  Serial.print(2000L + g_radio.getChannel());
  Serial.println(F(" MHz"));

  g_remote_clock_is_running = false;

  static uint8_t s_old_radio_mode = RADIO_MODE_OFF;
  if(s_old_radio_mode != g_radio_mode) {
    switch(g_radio_mode) {
    case RADIO_MODE_OFF:
      radio_off(); // puts the radio into TX mode, so it doesn't listen, but we don't send anything
      break;
    case RADIO_MODE_BROADCAST:
      radio_broadcast();
      break;
    case RADIO_MODE_LISTEN:
      radio_listen();
      break;
    }
    s_old_radio_mode = g_radio_mode;
    Serial.print(F("Updated radio mode to "));
    Serial.println(g_radio_mode);
  }
}

void load_settings() {
  g_brightness = EEPROM.read(EEPROM_BRIGHTNESS);
  g_horn_tenths = EEPROM.read(EEPROM_HORN_TENTHS);
  g_radio_mode = EEPROM.read(EEPROM_RADIO_MODE);
  g_radio_channel = EEPROM.read(EEPROM_RADIO_CHANNEL);

  // This handles default EEPROM values of 255
  if(g_brightness > MAX_BRIGHTNESS) g_brightness = DEFAULT_BRIGHTNESS;
  if(g_horn_tenths > MAX_HORN_TENTHS) g_horn_tenths = DEFAULT_HORN_TENTHS;
  if(g_radio_mode > MAX_RADIO_MODE) g_radio_mode = RADIO_MODE_BROADCAST;
  if(g_radio_channel > MAX_RADIO_CHANNEL) g_radio_channel = MIN_RADIO_CHANNEL;

  update_radio();
  
  set_led_brightness();
}

bool save_setting_if_changed(uint8_t eeprom_address, uint8_t current_value) {
  // Keep track if a change was made, for reporting back to the user.  EEPROM.update() doesn't tell
  // us.
  
  bool result = false;
  uint8_t stored_value = EEPROM.read(eeprom_address);
  if(current_value != stored_value) {
    if(g_debug) {
      Serial.print(F("save_setting_if_changed(): value change for eeprom address 0x"));
      Serial.print(eeprom_address, HEX);
      Serial.print(F(": old="));
      Serial.print(stored_value);
      Serial.print(F(" new="));
      Serial.println(current_value);
    }
      
    EEPROM.update(eeprom_address, current_value);
    result = true;
  }
  
  return result;
}

bool save_settings() {
  bool changes = false;
  // g_debug = true;
  changes = changes || save_setting_if_changed(EEPROM_BRIGHTNESS, g_brightness);
  changes = changes || save_setting_if_changed(EEPROM_HORN_TENTHS, g_horn_tenths);
  changes = changes || save_setting_if_changed(EEPROM_RADIO_MODE, g_radio_mode);
  changes = changes || save_setting_if_changed(EEPROM_RADIO_CHANNEL, g_radio_channel);
  load_settings();
  return changes;
}

void test_segment_table() {
  Serial.println("Testing all entries in the segment table.");

  int i = 0;
  uint8_t c;
  do {
    c = char_to_segment_table[i][0];
    Serial.print(i);
    if(c == 0) {
      Serial.print(F(" default=0b"));
    } else {
      sprintf_P(output_buf, PSTR(" %c=0b"), char_to_segment_table[i][0]);
      Serial.print(output_buf);
    }
    sprintf_P(output_buf, PSTR(BYTE_TO_BINARY_PATTERN), BYTE_TO_BINARY((char)lookup_segments(c)));
    Serial.println(output_buf);

    char data[4];
    data[0] = data[1] = data[2] = data[3] = c;
    set_display(&g_front_display, data);
    set_display(&g_rear_display, data);

    delay(500);

    i++;
  } while (c != 0);

  Serial.println(F("Finished."));
}

void test_read_buttons() {
  Serial.print(F("PIN_START_STOP_BUTTON: "));
  Serial.println(digitalRead(PIN_START_STOP_BUTTON));

  Serial.print(F("PIN_RESET_30_BUTTON: "));
  Serial.println(digitalRead(PIN_RESET_30_BUTTON));

  Serial.print(F("PIN_RESET_20_BUTTON: "));
  Serial.println(digitalRead(PIN_RESET_20_BUTTON));

  Serial.print(F("PIN_UP_BUTTON: "));
  Serial.println(digitalRead(PIN_UP_BUTTON));

  Serial.print(F("PIN_DOWN_BUTTON: "));
  Serial.println(digitalRead(PIN_DOWN_BUTTON));

  Serial.print(F("PIN_SETTINGS_BUTTON: "));
  Serial.println(digitalRead(PIN_SETTINGS_BUTTON));
}

uint8_t start_clock() {
  uint8_t rc = SUCCESS;
  g_last_clock_millis = millis();
  g_clock_is_running = true;
  g_front_display.animated = 1;
  g_rear_display.use_primary_buffer = 1;
  g_rear_display.animated = 0;
  g_front_display.use_primary_buffer = 1;
  return rc;
}

/* Update the clock millis every time through */
uint32_t update_clock_millis() {
  uint32_t current_millis= millis();
  uint32_t time_elapsed = 0;
  if(g_clock_is_running) {
    time_elapsed = current_millis - g_last_clock_millis;
  }
  g_last_clock_millis = current_millis;
  g_clock_millis -= time_elapsed;
}

void update_horn_state(uint32_t millis_elapsed) {
  if(g_horn_is_on) {
    if(g_horn_timer_millis > 0) {
      g_horn_timer_millis -= millis_elapsed;
      // Serial.print(F("Horn timer millis: "));
      // Serial.println(g_horn_timer_millis);
    } else {
      // horn off
      Serial.println(F("Horn OFF!"));
      digitalWrite(PIN_HORN_RELAY, LOW);
      g_horn_is_on = false;
    }
  }
}

void update_uptime(uint32_t current_millis) {
  if(current_millis - g_last_uptime_millis > 1000) {
    // we will never take more than a second to get back here.
    g_uptime_seconds++;
    g_last_uptime_millis = current_millis;
  }
}

void clear_button_events() {
  g_button_pressed_events = 0;
  g_button_released_events = 0;
}

void change_state(uint8_t new_state) {
  clear_button_events(); // every state change requires we reset the button inputs
  // command_inputs();
  g_state = new_state;

  displays_dirty();
}

void update_state_timeout(long addition) {
  g_state_timeout_millis = millis() + addition;
}

bool is_state_timeout_expired() {
  return millis() > g_state_timeout_millis;
}

void state_init() {
  static uint8_t s_init_state = INIT_STATE_WELCOME;

  if(g_state != STATE_INIT) {
    change_state(STATE_INIT);
    Serial.println(F("Type \"help\" for available commands."));

    push_single(MESSAGE_HELLO);
    command_show_message();

    print_radio_mode();

    s_init_state = INIT_STATE_SHOW_VERSION;
    update_state_timeout(INIT_DISPLAY_DELAY);
    return;
  }

  switch(s_init_state) {
  case INIT_STATE_SHOW_VERSION:
    if(g_state_timeout_millis < millis()) {
      push_single(MESSAGE_VERSION);
      command_show_message();
      update_state_timeout(INIT_DISPLAY_DELAY);
      s_init_state = INIT_STATE_SHOW_RADIO;
    }
    break;
  case INIT_STATE_SHOW_RADIO:
    if(is_state_timeout_expired()) {
    
      switch(g_radio_mode) {
      case RADIO_MODE_BROADCAST:
	push_single(MESSAGE_RADIO_MODE_BROADCAST);
	break;
      case RADIO_MODE_LISTEN:
	push_single(MESSAGE_RADIO_MODE_LISTEN);
	break;
      default:
	push_single(MESSAGE_RADIO_MODE_OFF);
	break;
      }
      command_show_message();
      g_state_timeout_millis = millis() + INIT_DISPLAY_DELAY;
      s_init_state = INIT_STATE_DONE;
    }
    break;
  case INIT_STATE_DONE:
    if(is_state_timeout_expired()) {
      command_reset_30();
      state_stopped();
    }
    break;
  }
}

void print_buttons(uint8_t buttons) {
  if(INPUT_START_STOP_BUTTON & buttons) { Serial.print(F(" INPUT_START_STOP_BUTTON")); }
  if(INPUT_RESET_30_BUTTON & buttons) { Serial.print(F(" INPUT_RESET_30_BUTTON")); }
  if(INPUT_RESET_20_BUTTON & buttons) { Serial.print(F(" INPUT_RESET_20_BUTTON")); }
  if(INPUT_UP_BUTTON & buttons) { Serial.print(F(" INPUT_UP_BUTTON")); }
  if(INPUT_DOWN_BUTTON & buttons) { Serial.print(F(" INPUT_DOWN_BUTTON")); }
  if(INPUT_SETTINGS_BUTTON & buttons) { Serial.print(F(" INPUT_SETTINGS_BUTTON")); }
}
 
void state_stopped() {
  if(g_state != STATE_STOPPED) {
    // Handle the state transition
    g_front_display.animated = 0;
    g_front_display.use_primary_buffer = 1;
    g_clock_is_running = false;

    if(g_state == STATE_RUNNING) {
      // transition from STATE_RUNNING to STATE_STOPPED
      if (g_clock_millis <= 0) {
	g_clock_millis = 0;
	// show the 0 on the clock
	// sound the horn
	Serial.println(F("HORN!"));
	command_beep();
      }
    }

    send_radio_command(RADIO_COMMAND_CLOCK_STOPPED);

    change_state(STATE_STOPPED);
    command_show_time();
    Serial.println(F("Stopped."));
  }

  receive_radio_message();
  
  /* No events, nothing to do here. */
  if((g_button_pressed_events | g_button_released_events) == 0)
    return;
  
  // For changing values, the general idea is that if one button is held
  // down, then the buttons on the other side are up and down.
  
  if(BUTTON_PRESSED_NO_MODS(INPUT_START_STOP_BUTTON)) {
    /*
    if(listening) {
      Serial.println(F("Can't start clock because button A was clicked: listening"));
      // TODO show [LS]/[LiSn]
    } else {
    */
    Serial.println(F("Starting clock because button A was clicked."));
    command_start_clock();
  }

  else if (BUTTON_PRESSED_NO_MODS(INPUT_RESET_30_BUTTON)) {
    /* if(listening) {
      } else { 
      Serial.println(F("Can't reset clock because button B was clicked: listening"));
      // TODO show [LS]/[LiSn]
      }*/
    command_reset_30();
  }

  else if (BUTTON_PRESSED_NO_MODS(INPUT_RESET_20_BUTTON)) {
    /*
    if(listening) {
    } else {
      Serial.println(F("Can't reset clock because button C was clicked: listening"));
      // TODO show [LS]/[LiSn]
      }*/
    command_reset_custom();
  }

  else if (BUTTON_PRESSED_NO_MODS(INPUT_UP_BUTTON)) {
    command_brightness_increase();
  }

  else if (BUTTON_PRESSED_NO_MODS(INPUT_DOWN_BUTTON)) {
    command_brightness_decrease();
  }

  else if (BUTTON_PRESSED_NO_MODS(INPUT_SETTINGS_BUTTON)) {
    state_setting();
  } 

  // Serial.println(F("CHECKING INPUT_START_STOP_BUTTON DOWN"));

  else if (BUTTON_DOWN(INPUT_START_STOP_BUTTON)) {
    // Make adjustments to the clock
    if (BUTTON_PRESSED(INPUT_UP_BUTTON)	|| BUTTON_PRESSED(INPUT_B)) {
      //if(!listening) {
      command_increase_time();
      //}
    } else if (BUTTON_PRESSED(INPUT_DOWN_BUTTON) || BUTTON_PRESSED(INPUT_D)) {
      //if(!listening) {
      command_decrease_time();
      //}
    } else if(BUTTON_PRESSED(INPUT_C)) {
      // might do autorun on/off
    }
  }

  // Serial.println(F("CHECKING INPUT_RESET_30_BUTTON DOWN"));
  
  else if (BUTTON_DOWN(INPUT_RESET_30_BUTTON)) {
    if(BUTTON_PRESSED(INPUT_A)) {
      command_brightness_increase();
    }

    else if (BUTTON_PRESSED(INPUT_C)) {
      command_brightness_decrease();
    }

    else if(BUTTON_PRESSED(INPUT_D)) {
      // available
    }
  }

  // Serial.println(F("CHECKING INPUT_RESET_20_BUTTON DOWN"));
  
  else if (BUTTON_DOWN(INPUT_RESET_20_BUTTON)) {

    if (BUTTON_PRESSED(INPUT_UP_BUTTON)	|| BUTTON_PRESSED(INPUT_B)) {
      command_increase_custom_reset_clock();
    }

    else if (BUTTON_PRESSED(INPUT_DOWN_BUTTON) || BUTTON_PRESSED(INPUT_D)) {
      command_decrease_custom_reset_clock();
    }

    else if (BUTTON_PRESSED(INPUT_A)) {
      command_read_temperature();
      command_show_number_transitory();
    }
  }
}

void send_radio_command_show_time_if_necessary() {
  if(g_radio_mode != RADIO_MODE_BROADCAST) {
    return;
  }

  bool send_message_flag = false;

  /* 
     If the display has changed, we send a message 
  */

  static char old_front_left = ' ';
  static char old_front_right = ' ';
  char current_front_left = g_front_display.primary_buffer[2];
  char current_front_right = g_front_display.primary_buffer[3];

  if((old_front_left != current_front_left) || (old_front_right != current_front_right)) {
    send_message_flag = true;
  }

  if(send_message_flag) {
    Serial.print(F("old display:["));
    Serial.print(old_front_left);
    Serial.print(old_front_right);
    Serial.print(F("] current:["));
    Serial.print(current_front_left);
    Serial.print(current_front_right);
    Serial.print(F("] beep:"));
    Serial.print(g_horn_is_on);
    Serial.println();

    send_radio_command(RADIO_COMMAND_SHOW_TIME);

    old_front_left = current_front_left;
    old_front_right = current_front_right;
  }
}

void state_running() {
  
  if(g_state != STATE_RUNNING) {
    start_clock();
    update_clock_millis();
    g_front_display.use_primary_buffer = 1;
    g_front_display.animated = 1;
    change_state(STATE_RUNNING);
    Serial.println(F("Running."));
    send_radio_command(RADIO_COMMAND_CLOCK_STARTED);
  } 

  update_clock_millis();
  command_show_time();

  if (g_clock_millis <= 0) {
    Serial.println(F("Stopping clock because timer hit 0."));
    //clear_button_events();
    command_stop_clock();
  } 

  else if (BUTTON_PRESSED(INPUT_START_STOP_BUTTON)) {
    Serial.println(F("Stopping clock because button A was pressed."));
    //clear_button_events();
    command_stop_clock();
  }

  else if (BUTTON_PRESSED(INPUT_RESET_30_BUTTON)) {
    command_reset_30();
    //  clear_button_events();
  }

  else if (BUTTON_PRESSED(INPUT_RESET_20_BUTTON)) {
    command_reset_custom();
    // clear_button_events();
  }

  send_radio_command_show_time_if_necessary();
}

void show_setting_value(uint8_t setting_state) {
  // Serial.print(F("show_setting_value(): setting_state="));
  // Serial.print(setting_state);
  // Serial.print(F(" "));

  switch_to_primary_buffer(); // don't get caught writing to transitory buffer

  switch(setting_state) {
  case SETTING_STATE_HORN:
    push_single(g_horn_tenths);
    command_show_number();
    break;
  case SETTING_STATE_COLOR_MODE:
    push_single(MESSAGE_COLOR_MODE_CUSTOM + g_color_mode);
    command_show_message();
    break;
  case SETTING_STATE_RADIO_MODE:
    push_single(MESSAGE_RADIO_MODE_OFF + g_radio_mode);
    command_show_message();
    //command_state();
    break;
  case SETTING_STATE_RADIO_CHANNEL:
    push_single(g_radio_channel);
    command_show_number();
    break;
  case SETTING_STATE_RADIO_STRENGTH:
    push_single(g_radio_signal_strength);
    command_show_number();
    break;
  }
}

void show_setting_name(uint8_t setting_state) {
  // What we'll do is show the setting name in the transitory, which will timeout
  // and show the actual value.
  push_single(MESSAGE_SETTING_HORN + setting_state);
  command_show_message_transitory();
}

void wrap_range(int8_t *value, int8_t min, int8_t max) {
  if((*value) > max) *value = min;
  if((*value) < min) *value = max;
}

void state_setting() {
  static uint8_t s_setting_state = SETTING_STATE_HORN;
  static bool s_signal_strength_test_running = false;
  
  // command_inputs();
  // Serial.println();
  
  if(g_state != STATE_SETTING) {

    g_front_display.use_primary_buffer = 1;
    g_front_display.animated = 0;
    change_state(STATE_SETTING);
    s_setting_state = SETTING_STATE_HORN;
    update_state_timeout(SETTING_TIMEOUT_MILLIS);
    show_setting_value(s_setting_state);
    show_setting_name(s_setting_state);
    return;
  }

  if(is_state_timeout_expired()) {
    if(s_setting_state != SETTING_SAVED) {
      s_setting_state = SETTING_SAVED;
      bool changes = save_settings();
      // If there were any changes, show MESSAGE_SAVE
      if(changes) {
	push_single(MESSAGE_SAVE);
	command_show_message();
	update_state_timeout(DEFAULT_TRANSITORY_DISPLAY_MILLIS);
      } else {
	Serial.println(F("No changes to settings"));
      }
    } else {
      Serial.println(F("Leaving settings"));
      state_stopped();
    }
    return;
  }

  if (BUTTON_PRESSED_NO_MODS(INPUT_SETTINGS_BUTTON)) {
    update_state_timeout(DEFAULT_TRANSITORY_DISPLAY_MILLIS + SETTING_TIMEOUT_MILLIS);

    Serial.println("state_setting(): settings button pressed");

    // advance to the next SETTING
    s_setting_state++;
    wrap_range(&s_setting_state, SETTING_STATE_HORN, MAX_SETTING_STATE);
    show_setting_value(s_setting_state);
    show_setting_name(s_setting_state);
    Serial.print(F("state_setting(): Showing setting "));
    Serial.println(s_setting_state);
    //command_state();

    if(s_setting_state == SETTING_STATE_RADIO_STRENGTH) {
      s_signal_strength_test_running = prepare_radio_signal_test();
    } else {
      s_signal_strength_test_running = false;
    }
  } else if (BUTTON_DOWN(INPUT_SETTINGS_BUTTON)) {
    update_state_timeout(SETTING_TIMEOUT_MILLIS);
  }
  else if(s_setting_state == SETTING_STATE_RADIO_STRENGTH) {
    // I need a little delay before this starts.  Check the transitory buffer value.
    if(s_signal_strength_test_running && (g_front_display.transitory_timer_millis <= 0)) {
      s_signal_strength_test_running = send_test_packet();
      show_setting_value(s_setting_state);
      if(!s_signal_strength_test_running) {
	complete_radio_signal_test();
      }
      update_state_timeout(SETTING_TIMEOUT_MILLIS);
    } 
  }
  else if (BUTTON_PRESSED(INPUT_UP_BUTTON) || BUTTON_PRESSED(INPUT_A)) {
    // Make adjustments to the current setting value
    update_state_timeout(SETTING_TIMEOUT_MILLIS);

    // increase the current setting
    Serial.println(F("state_setting(): UP"));
    switch(s_setting_state) {
    case SETTING_STATE_HORN:
      g_horn_tenths++;
      wrap_range(&g_horn_tenths, 0, MAX_HORN_TENTHS);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_COLOR_MODE:
      g_color_mode++;
      wrap_range(&g_color_mode, COLOR_MODE_WHITE, MAX_COLOR_MODE);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_RADIO_MODE:
      g_radio_mode++;
      wrap_range(&g_radio_mode, MIN_RADIO_MODE, MAX_RADIO_MODE);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_RADIO_CHANNEL:
      g_radio_channel++;
      wrap_range(&g_radio_channel, MIN_RADIO_CHANNEL, MAX_RADIO_CHANNEL);
      show_setting_value(s_setting_state);
      break;
    }
    displays_dirty();
  }

  else if (BUTTON_PRESSED(INPUT_DOWN_BUTTON) || BUTTON_PRESSED(INPUT_C)) {
    update_state_timeout(SETTING_TIMEOUT_MILLIS);

    Serial.println(F("state_setting(): DOWN"));
    switch(s_setting_state) {
    case SETTING_STATE_HORN:
      g_horn_tenths--;
      wrap_range(&g_horn_tenths, 0, MAX_HORN_TENTHS);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_COLOR_MODE:
      g_color_mode--;
      wrap_range(&g_color_mode, COLOR_MODE_WHITE, MAX_COLOR_MODE);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_RADIO_MODE:
      g_radio_mode--;
      wrap_range(&g_radio_mode, MIN_RADIO_MODE, MAX_RADIO_MODE);
      show_setting_value(s_setting_state);
      break;
    case SETTING_STATE_RADIO_CHANNEL:
      g_radio_channel--;
      wrap_range(&g_radio_channel, MIN_RADIO_CHANNEL, MAX_RADIO_CHANNEL);
      show_setting_value(s_setting_state);
      break;
    }
    displays_dirty();
  } 
}


long g_remote_timer_millis = 0;
long g_last_contact_millis = 0;

void refresh_display(struct display_info *display, long millis_elapsed) {
  if (display->requires_refresh) {
    /* check refresh timer to update display */
    display->refresh_millis -= millis_elapsed;
    if(display->refresh_millis <= 0) {
      display_dirty(display);
      if(display->animated) {
	display->refresh_millis = ANIMATION_REFRESH_INTERVAL_MILLIS;
      } else {
	display->refresh_millis = DEFAULT_REFRESH_INTERVAL_MILLIS;
      }
    }
  }

  /* check timer for transitory to primary display buffer switch */
  if(!display->use_primary_buffer) {
    //Serial.print(F("TRANSITORY_TIMER_MILLIS: "));
    //Serial.println(display->transitory_timer_millis);
    display->transitory_timer_millis -= millis_elapsed;
    if(display->transitory_timer_millis <= 0) {
      switch_to_primary_buffer();
    }
  }

  update_display(display);
}

#define DEFAULT_RECOMMENDED_REFRESH 200 // millis
#define QUICK_RECOMMENDED_REFRESH 20 // millis

uint8_t checksum_bytes(uint8_t *b, int size) {
  uint8_t checksum = 0;
  for (int i = 0; i < size; i++) {
    /*
    Serial.print(F("b["));
    Serial.print(i);
    Serial.print(F("]="));
    Serial.print(b[i]);
    Serial.print(F(" "));
    */
    checksum += b[i];
  }
  //  Serial.println();
  return checksum;
}

uint8_t checksum_radio_message() {
  return checksum_bytes((uint8_t *) &g_radio_message, sizeof(g_radio_message) - 1);
}

void print_radio_mode() {
  Serial.print(F("Radio mode: "));
  switch(g_radio_mode) {
  case RADIO_MODE_OFF:
    Serial.println(F("OFF"));
    break;
  case RADIO_MODE_BROADCAST:
    Serial.println(F("BROADCAST"));
    break;
  case RADIO_MODE_LISTEN:
    Serial.println(F("LISTEN"));
    break;
  }
}

void print_radio_message() {
  Serial.print(F(" sender_serial_number="));
  Serial.print(g_radio_message.sender_serial_number);
  Serial.print(F(" message_serial_number="));
  Serial.print(g_radio_message.message_serial_number);
  Serial.print(F(" command="));
  Serial.print(g_radio_message.command);
  Serial.print(F(" clock_millis="));
  Serial.print(g_radio_message.clock_millis);
  Serial.print(F(" clock_running="));
  Serial.print(g_radio_message.clock_running);
  Serial.print(F(" checksum="));
  Serial.print(g_radio_message.checksum);
}

void receive_radio_message() {
  if(!g_radio_ok)
    return;
  if(g_radio_mode != RADIO_MODE_LISTEN)
    return;
  
  uint8_t pipe;
  if (g_radio.available(&pipe)) { 
    g_radio.read(&g_radio_message, sizeof(g_radio_message)); // read and send ACK
    print_radio_message();
    Serial.println();
    
    if(g_radio_message.checksum != checksum_radio_message()) {
      Serial.print(F("BAD CHECKSUM ON RECEIVED RADIO MESSAGE: "));
      Serial.println();
    } else {
      // check that the data makes sense
      if(g_radio_message.command > MAX_RADIO_COMMAND) {
	Serial.println(F("received command out of range"));
	return;
      }
      if((g_radio_message.clock_millis < -2000) || (g_radio_message.clock_millis >= 100000)) {
	Serial.println(F("received clock_millis out of range"));
	return;
      }
      if(g_radio_message.clock_running > 1) {
	Serial.println(F("received clock_running out of range"));
	return;
      }
      
      g_clock_millis = g_radio_message.clock_millis;
      g_remote_clock_is_running = g_radio_message.clock_running;

      switch(g_radio_message.command) {
      case RADIO_COMMAND_SHOW_TIME:
	command_show_time();
	break;
      case RADIO_COMMAND_BEEP:
	command_beep();
	break;
      case RADIO_COMMAND_CLOCK_STARTED:
	Serial.println(F("Remote clock started"));
	displays_dirty();
	break;
      case RADIO_COMMAND_CLOCK_STOPPED:
	Serial.println(F("Remote clock stopped"));
	displays_dirty();
	break;
      case RADIO_COMMAND_SIGNAL_TEST:
	Serial.print(F("."));
	break;
      }
    }
  }
}

bool send_radio_command(uint8_t radio_command) {
  bool result = false;
  if(!g_radio_ok)
    return false;
  if(g_radio_mode != RADIO_MODE_BROADCAST)
    return false;

  unsigned long start_timer, end_timer;
  unsigned long timeout_millis = millis() + MAX_TRANSMISSION_MILLIS;

  int loop_max = 1 + MAX_TRANSMISSION_RETRIES;

  if(radio_command == RADIO_COMMAND_SIGNAL_TEST)
    loop_max = 1;
  
  for(int i = 0; i < loop_max; i++) {
    wdt_reset(); // things could take a while
    
    g_radio_message.sender_serial_number = 111;
    g_radio_message.message_serial_number = ++g_message_serial_number;
    g_radio_message.clock_millis = g_clock_millis;
    g_radio_message.clock_running = g_clock_is_running;
    g_radio_message.command = radio_command;
    g_radio_message.checksum = checksum_radio_message();
  
    start_timer = micros();
    result = g_radio.write(&g_radio_message, sizeof(g_radio_message));
    end_timer = micros();

    if(g_debug) {
      print_radio_message();
      Serial.println();
    }
  
    if (!result) {
      // payload was not delivered
      if(g_debug) {
	Serial.print(F("Transmission failed or timed out after "));
	Serial.print(end_timer - start_timer);
	Serial.println(F(" microseconds"));
      }
      if(millis() > timeout_millis) {
	//Serial.println(F("Transmission timeout."));
	return result;
      }	
    } else {
      return result;
    }
  }
  return result;
}

uint8_t g_test_packet_count = 0;

bool prepare_radio_signal_test() {
  g_radio_signal_strength = 0;
  g_test_packet_count = 0;

  if(!g_radio_ok) {
    Serial.println(F("Radio not OK."));
    return false;
  }
  if(g_radio_mode != RADIO_MODE_BROADCAST) {
    Serial.println(F("First put radio in broadcasting mode."));
    return false;
  }
  
  // g_radio.setRetries(0, 0);
  return true;
}

bool send_test_packet() {
  // return TRUE while there are more test packets to send.
  if(g_test_packet_count >= 99)
    return false;

  wdt_reset();
  g_test_packet_count++;
  if(send_radio_command(RADIO_COMMAND_SIGNAL_TEST)) {
    g_radio_signal_strength++;	
    Serial.print(F("+"));
  } else {
    Serial.print(F("-"));
  }
  return true;
}

void complete_radio_signal_test() {
  g_radio.setRetries(RADIO_DELAY, RADIO_RETRIES);
}

void loop() {

  wdt_reset();
  
  // Grab the current inputs as updated by the pin change ISRs, only here.
  g_inputs = g_inputs_volatile;

  // Any button state changes since the last?
  uint8_t transitions = g_inputs ^ g_last_inputs;
  g_last_inputs = g_inputs;

  if (transitions) {
    // let's see what happened that we need to handle
    g_button_pressed_events = g_inputs & transitions;
    g_button_released_events = ~g_inputs & transitions;
    // if(g_debug) {
    //   Serial.println("====================TRANSITIONS==================");
    //   command_inputs(); // show the inputs and history for debug
    // }
  }

  /* color mode animation needs rapid refresh */
  if(g_color_mode > 4) {
    g_front_display.animated = 1;
  }
  
  /* Time updates for the loop. */
  uint32_t current_time = millis();
  uint32_t millis_elapsed = current_time - g_last_loop_millis;
  g_last_loop_millis = current_time;

  // if the state has changed, we need to mark the display dirty.
  refresh_display(&g_front_display, millis_elapsed);
  refresh_display(&g_rear_display, millis_elapsed);

  switch(g_state) {
  case STATE_UNINITIALIZED:
    state_init();
    break;
  case STATE_INIT:
    state_init();
    break;
  case STATE_STOPPED:
    state_stopped();
    break;
  case STATE_RUNNING:
    state_running();
    break;
  case STATE_SETTING:
    state_setting();
    break;
  default:
    break;
  }

  // if(g_debug) Serial.println(F("loop(): Done handling g_state"));

  update_horn_state(millis_elapsed);
  update_uptime(current_time);

  process_serial_input();

  // do this at the end, so we don't erase the state of the inputs for the command processor to see
  clear_button_events();

  // if(g_debug) {
  //   Serial.println(F("loop(): at end.  inputs:"));
  //   command_inputs();
  // }
}

void print_inputs(uint8_t inputs)  {
  for (int16_t n = 0; n < 8; n++) {
    Serial.print(inputs & bit(7) ? 1 : 0);
    inputs = inputs << 1;
  }
  Serial.println();
}


/*
  Segments:
   _____
  <__A__>
 /\     /\
 | |   | |
 |F|   |B|
 | |   | |
 \/____ \/
  <__G__>
 /\     /\
 | |   | |
 |E|   |C|
 | |   | |
 \/_____\/
  <__D__>

  The way the LED string is wired up, it goes counter-clockwise from E:
  EDCBAFG. So you need to keep that in mind as you specify segment pixel offsets.
 
  We could start the wiring at A, but the pixel string needs to go in a continuous spiral,
  so we would have to splice.
*/


void neopixel_segment(Adafruit_NeoPixel *pixels, uint8_t segment) {
  for (int16_t i = 0; i < PIXELS_PER_SEGMENT; i++) {
    neopixel_segment_pixel(pixels, segment, i);
  }
}

void neopixel_segments(Adafruit_NeoPixel *pixels, uint8_t segments) {
  /*
    This is for turning on entire segments.
    segments follows the format in TM1637Display.h, e.g.:
    {'    // 0',},
    0b00000110,    // 1
    etc.
  */

  // this is no different from scanning a table.
  
  if(segments & SEG_A) {
    neopixel_segment(pixels, SEGMENT_A);
  }
  if(segments & SEG_B) {
    neopixel_segment(pixels, SEGMENT_B);
  }
  if(segments & SEG_C) {
    neopixel_segment(pixels, SEGMENT_C);
  }
  if(segments & SEG_D) {
    neopixel_segment(pixels, SEGMENT_D);
  }
  if(segments & SEG_E) {
    neopixel_segment(pixels, SEGMENT_E);
  }
  if(segments & SEG_F) {
    neopixel_segment(pixels, SEGMENT_F);
  }
  if(segments & SEG_G) {
    neopixel_segment(pixels, SEGMENT_G);
  }
}


uint8_t lookup_segments(uint8_t c) {
  int i = 0;
  uint8_t b;
  while((b = char_to_segment_table[i][0]) != 0) {
    if(b == c)
      return char_to_segment_table[i][1];
    else
      i++;
  }

  return char_to_segment_table[i][1]; //default
}

void print_segment_lookup_table() {
  Serial.println("print_segment_lookup_table()");

  int i = 0;
  uint8_t c;
  do {
    c = char_to_segment_table[i][0];
    Serial.print(i);
    if(c == 0) {
      Serial.print(F(" default=0b"));
    } else {
      sprintf_P(output_buf, PSTR(" %c=0b"), char_to_segment_table[i][0]);
      Serial.print(output_buf);
    }
    sprintf_P(output_buf, PSTR(BYTE_TO_BINARY_PATTERN), BYTE_TO_BINARY((char)lookup_segments(c)));
    Serial.println(output_buf);
    i++;
  } while (c != 0);
}

void led_pixel(Adafruit_NeoPixel *pixels, uint8_t pixel_index) {
  update_rgb(pixel_index); // alter colors for visual effects
  pixels->setPixelColor(pixel_index, g_color.wrgb);
}

void neopixel_segment_pixel(Adafruit_NeoPixel *pixels, uint8_t segment, uint8_t offset) {
  uint8_t pixel_index = PIXELS_PER_SEGMENT * segment + offset;
  led_pixel(pixels, pixel_index);
}

void  display_neopixels_char(Adafruit_NeoPixel *pixels, char c) {
  pixels->clear();

  if(g_clock_is_running || g_remote_clock_is_running) {
    // turn on the last pixels in each string as the running indicator.
    // One goes to the front, one to the back.
    led_pixel(pixels, LED_COUNT - 1);
  }

  uint8_t segments = lookup_segments(c);
  if(segments != EOF) {
    neopixel_segments(pixels, segments);

    // Additional pixels we wish to light up
    if (c=='.') {
      neopixel_segment_pixel(pixels, SEGMENT_C, 0);
    } else if (c=='b') {
      neopixel_segment_pixel(pixels, SEGMENT_A, PIXELS_PER_SEGMENT-1);
    } else if (c=='C') {
      neopixel_segment_pixel(pixels, SEGMENT_B, PIXELS_PER_SEGMENT-1);
      neopixel_segment_pixel(pixels, SEGMENT_C, 0);
    } else if (c=='d') {
      neopixel_segment_pixel(pixels, SEGMENT_A, 0);
    } else if (c=='h') {
      neopixel_segment_pixel(pixels, SEGMENT_A, PIXELS_PER_SEGMENT-1);
    } else if (c=='i') {
      neopixel_segment_pixel(pixels, SEGMENT_F, PIXELS_PER_SEGMENT-2);
      neopixel_segment_pixel(pixels, SEGMENT_D, 0);
    } else if (c=='j') {
      neopixel_segment_pixel(pixels, SEGMENT_B, 2);
    } else if (c=='n') {
      neopixel_segment_pixel(pixels, SEGMENT_F, PIXELS_PER_SEGMENT-1);
    } else if (c=='r') {
      neopixel_segment_pixel(pixels, SEGMENT_F, PIXELS_PER_SEGMENT-1);
    } else if (c=='S') {
      neopixel_segment_pixel(pixels, SEGMENT_B, PIXELS_PER_SEGMENT-1);
      neopixel_segment_pixel(pixels, SEGMENT_E, PIXELS_PER_SEGMENT-1);
    } else if (c=='t') {
      neopixel_segment_pixel(pixels, SEGMENT_G, 0);
      neopixel_segment_pixel(pixels, SEGMENT_G, 1);
      neopixel_segment_pixel(pixels, SEGMENT_G, 2);
    }

    pixels->show();
    // delay(50);
    // Just be careful not to update the display too often.
    // We update it every .1 seconds.
  }
}


void update_rgb(uint8_t pixel_index) {
  // depending on the easter egg mode, we change colors.
  switch(g_color_mode) {	
  case(COLOR_MODE_WHITE):
    g_color.parts.red = 255; g_color.parts.blue = 255; g_color.parts.green = 255;
    break;
  case(COLOR_MODE_RED):
    g_color.parts.red = 255; g_color.parts.green = 0; g_color.parts.blue = 0; 
    break;
  case(COLOR_MODE_GREEN):
    g_color.parts.red = 0; g_color.parts.green = 255; g_color.parts.blue = 0; 

    break;
  case(COLOR_MODE_BLUE):
    g_color.parts.red = 0; g_color.parts.green = 0; g_color.parts.blue = 255; 
    break;
  case(COLOR_MODE_COLOR_FADE):
    color_fade();
    break;
  case(COLOR_MODE_RAINBOW):
    rainbow(pixel_index);
    break;
  default:
    // COLOR_MODE_NONE
    // don't change it, it was set with color!
    break;
  }
}

void display_tm1637_string(char *buf) {
  char segment_data[4]; // tm1637 has only 4 characters
  for (int i=0; i<4; i++) {
    segment_data[i] =lookup_segments(buf[i]);

    // Override to turn on bottom segment to represent a '.'
    // Rather do this here than complicate the code for the LEDs.
    if(buf[i]=='.') {
      segment_data[i] |= SEG_D;
    }
  }    
  if(g_clock_is_running) {
    // show the colon
    segment_data[1] |= SEG_DP;
  }

  g_tm1637_display.setSegments(segment_data);
}

void fill_display_buffer(struct display_info *display, char *buffer, char *contents) {
  memcpy(buffer, contents, display->buffer_size);
}  

int compare_display_buffer(struct display_info *display, char *buffer, char *contents) {
  int i;
  for (i = 0; i<display->buffer_size; i++) {
    if(buffer[i] > contents[i]) return 1;
    if(buffer[i] < contents[i]) return -1;
  }
  return 0;
}  

int compare_active_display_buffer(struct display_info *display, char *contents) {
  char *display_buf = display->use_primary_buffer ? 
    display->primary_buffer : display->transitory_buffer;
  return compare_display_buffer(display, display_buf, contents);
}

void fill_active_display_buffer(struct display_info *display, char *contents) {
  char *display_buf = display->use_primary_buffer ? 
    display->primary_buffer : display->transitory_buffer;
  fill_display_buffer(display, display_buf, contents);
}

void fill_primary_display_buffer(struct display_info *display, char *contents) {
  fill_display_buffer(display, display->primary_buffer, contents);
}  

void fill_transitory_display_buffer(struct display_info *display, char *contents) {
  fill_display_buffer(display, display->transitory_buffer, contents);
}  

void switch_to_transitory_buffer() {
  g_front_display.use_primary_buffer = g_rear_display.use_primary_buffer = 0;
  g_front_display.transitory_timer_millis = g_rear_display.transitory_timer_millis = DEFAULT_TRANSITORY_DISPLAY_MILLIS;
  displays_dirty();
}

void switch_to_primary_buffer() {
  g_front_display.use_primary_buffer = g_rear_display.use_primary_buffer = 1;
  g_front_display.transitory_timer_millis = g_rear_display.transitory_timer_millis = 0;
  displays_dirty();
}

void update_front_display(char *display_buf) {
  display_neopixels_char(&g_left_digit, display_buf[2]);
  display_neopixels_char(&g_right_digit, display_buf[3]);
}

void update_rear_display(char *display_buf) {
  display_tm1637_string(display_buf);
}

void update_display(struct display_info *display) {
  char *display_buf;
  if (display->dirty) {
    display_buf = display->use_primary_buffer ? 
      display->primary_buffer : display->transitory_buffer;

    if(display == &g_front_display) {
      update_front_display(display_buf);
    } else {
      update_rear_display(display_buf);
    }
    display->dirty = 0;
  }
}

void set_display(struct display_info *display, char* contents) {
  if(compare_active_display_buffer(display, contents) != 0) {
    display_dirty(display);
  }
  
  fill_active_display_buffer(display, contents);
  update_display(display);

  /*
  Serial.print(F("set_display(): "));
  if(display == &g_front_display) {
    Serial.print(F("front"));
  }
  if(display == &g_rear_display) {
    Serial.print(F("rear"));
  }
    
  Serial.print(F(" "));
  Serial.print(contents[0]);
  Serial.print(contents[1]);
  Serial.print(contents[2]);
  Serial.print(contents[3]);
  Serial.println();
  */
}

void set_led_brightness() {
  uint8_t led_brightness = 255; 

  // See c:/Users/dnh/Documents/Arduino/libraries/Adafruit_NeoPixel/Adafruit_NeoPixel.cpp
  // but the comments seem wrong; 0 is not max, it is off.
  // 1 = min brightness (off), 255 = max.

  // My brightness is 1-5, where 5 is max.
  switch(g_brightness) {
  case 1:led_brightness = 16; break;
  case 2: led_brightness = 32; break;
  case 3: led_brightness = 64; break;
  case 4: led_brightness = 128; break;
  default: led_brightness = 255; break;
  }

  g_left_digit.setBrightness(led_brightness);
  g_right_digit.setBrightness(led_brightness);

  display_dirty(&g_front_display);
  
  /*
    Serial.print(F("g_brightness is "));
    Serial.print(g_brightness);
    Serial.print(F(", set LED brightness to "));
    Serial.println(led_brightness);
  */
}

// Color fade cycle along whole strip. 
long g_pixel_hue = 0;

void color_fade() {
  // called for each pixel update
  g_pixel_hue += 3;
  g_color.wrgb = g_right_digit.gamma32(g_right_digit.ColorHSV(g_pixel_hue));
}

#define VIOLET 0x009400D3	 // 	148, 0, 211	#
#define INDIGO 0x004B0082	 // 	75, 0, 130	#
#define BLUE   0x000000FF	 // 	0, 0, 255	#
#define GREEN  0x0000FF00	 // 	0, 255, 0	#
#define YELLOW 0x00FFFF00	 // 	255, 255, 0	#
#define ORANGE 0x00FF7F00	 // 	255, 127, 0	#
#define RED    0x00FF0000        // 	255, 0 , 0 #
#define RAINBOW_COLOR_COUNT 7

#define RAINBOW_DELAY 100

uint32_t g_rainbow_last_frame = 0;
uint32_t g_rainbow_last_update_millis = 0;
uint8_t g_rainbow_starting_color_index = 0;

void rainbow(uint8_t pixel_index) {
  uint8_t color_index = 0;
  uint32_t current_millis = millis();
  
  if(current_millis > g_rainbow_last_update_millis + RAINBOW_DELAY) {
    // move the pixel index along
    g_rainbow_starting_color_index++;
    if (g_rainbow_starting_color_index >= RAINBOW_COLOR_COUNT) {
      g_rainbow_starting_color_index = 0;
    }

    // make sure the display refresh is adequate to maintain the animation
    g_rainbow_last_update_millis = current_millis;
  }

  color_index = (pixel_index + g_rainbow_starting_color_index) % RAINBOW_COLOR_COUNT;
  if(color_index == 0) g_color.wrgb = VIOLET;
  if(color_index == 1) g_color.wrgb = INDIGO;
  if(color_index == 2) g_color.wrgb = BLUE;
  if(color_index == 3) g_color.wrgb = GREEN;
  if(color_index == 4) g_color.wrgb = YELLOW;
  if(color_index == 5) g_color.wrgb = ORANGE;
  if(color_index == 6) g_color.wrgb = RED;
}
