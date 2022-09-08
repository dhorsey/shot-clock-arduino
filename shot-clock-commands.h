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

#define BYTE_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c"
#define BYTE_TO_BINARY(byte)  \
  (byte & 0x80 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x01 ? '1' : '0')

#define BYTE_TO_BINARY_REVERSE(byte)  \
  (byte & 0x01 ? '1' : '0'), \
  (byte & 0x02 ? '1' : '0'), \
  (byte & 0x04 ? '1' : '0'), \
  (byte & 0x08 ? '1' : '0'), \
  (byte & 0x10 ? '1' : '0'), \
  (byte & 0x20 ? '1' : '0'), \
  (byte & 0x40 ? '1' : '0'), \
  (byte & 0x80 ? '1' : '0')

#define WORD_TO_BINARY_PATTERN "%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c%c"
#define WORD_TO_BINARY(word)  \
  (word & 0x8000 ? '1' : '0'), \
  (word & 0x4000 ? '1' : '0'), \
  (word & 0x2000 ? '1' : '0'), \
  (word & 0x1000 ? '1' : '0'), \
  (word & 0x0800 ? '1' : '0'), \
  (word & 0x0400 ? '1' : '0'), \
  (word & 0x0200 ? '1' : '0'), \
  (word & 0x0100 ? '1' : '0'), \
  (word & 0x0080 ? '1' : '0'), \
  (word & 0x0040 ? '1' : '0'), \
  (word & 0x0020 ? '1' : '0'), \
  (word & 0x0010 ? '1' : '0'), \
  (word & 0x0008 ? '1' : '0'), \
  (word & 0x0004 ? '1' : '0'), \
  (word & 0x0002 ? '1' : '0'), \
  (word & 0x0001 ? '1' : '0')

#define LAST_3_DIGITS(n) \
  ((n/1000) % 10), \
  ((n/100) % 10), \
  (n % 10)
    
void command_scan_i2c(void);
void command_read_temperature(void);
void command_show_front(void);
void command_show_front_transitory(void);
void command_show_rear(void);
void command_show_rear_transitory(void);
void command_two_digits(void);
void command_show_number(void);
void command_show_number_transitory(void);
void command_show_time(void);
void command_increase_time(void);
void command_decrease_time(void);
void command_reset_30(void);
void command_reset_custom(void);
void command_increase_custom_reset_clock(void);
void command_decrease_custom_reset_clock(void);
void command_start_clock(void);
void command_stop_clock(void);
void command_set_clock(void);
void command_clock(void);
void command_horn(void);
void command_beep(void);
void command_horn_set(void);
void command_horn_get(void);
void command_horn_increase(void);
void command_horn_decrease(void);
void command_hms(void);
void command_uptime(void);
void command_settings_load(void);
void command_settings_save(void);
void command_reset_settings(void);
void command_brightness_set(void);
void command_brightness_get(void);
void command_brightness_increase(void);
void command_brightness_decrease(void);
void command_color(void);
void command_color_get(void);
void command_color_set(void);
void command_color_mode_get(void);
void command_color_mode_set(void);
void command_state(void);
void command_inputs(void);
void command_radio_off(void);
void command_radio_broadcast(void);
void command_radio_listen(void);
void command_radio_signal_test(void);
void command_radio(void);
void command_show_message(void);
void command_show_message_transitory(void);
void command_print_message(void);
