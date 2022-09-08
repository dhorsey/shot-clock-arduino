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

#define INPUT_BUFFER_SIZE 80
#define OUTPUT_BUFFER_SIZE 80
#define DELIMETERS " "
#define MAX_TOKENS 20
#define DATA_STACK_MAX 32

#define SUCCESS 0
#define ERROR 255
#define ERROR_WORD_NOT_FOUND 1
#define ERROR_STACK_OVERFLOW 2
#define ERROR_STACK_UNDERFLOW 3
#define ERROR_BAD_DICTIONARY_INDEX 4
#define ERROR_UNKNOWN_TYPE 5

// We define 3 dictionaries:
// g_base_dictionary : all of the forth words, in flash
// g_application_dictionary : all of the primitive application words, in flash
// g_user_dictionary: all of the user-defined words, in SRAM

// Instead of putting a pointer to the code to execute in the dictionary,
// we can save a few bytes and have pretty good type safety by using
// a byte to keep track of the type of the entry.  This also avoids having
// multiple dictionaries for each type, or to duplicate lookup code.

#define TYPE_COMMAND 'x'
#define TYPE_CONSTANT '0'
#define TYPE_VALUE 'v'
#define TYPE_CVALUE 'c'
#define TYPE_DVALUE 'd'
#define TYPE_END_OF_DICT '\0'

struct help_entry {
  const char *name;           // pointer to name of command, in flash
  const char *help;           // help string in Flash
};

struct dictionary_entry {
  const char *name;           // pointer to name of command, in flash
  char type;            // for runtime type checking.
  union {
    void (*command)(void); // function pointer, args and return values are all on data stack
    int16_t constant; 
    int8_t *cvalue; 
    int16_t *value; 
    int32_t *dvalue; 
  } cell;
};

union double_word {
  int32_t signed_double_word;
  uint16_t unsigned_word[2];
};

void Serial_P(const char* pstr); // output a flash string to Serial

void process_serial_input(void);
void command_interpret(void);

void push_single(int16_t);
void push_two_singles(int16_t, int16_t);
void push_four_singles(int16_t, int16_t, int16_t, int16_t);
int16_t pop_single();
void push_double(int32_t);
int32_t pop_double();

void print_single(int16_t);
void print_double(int32_t);

void command_print_data_stack(void);
void command_pop(void);
void command_pop_double(void);
void command_words(void);
void command_plus(void);
void command_plus_double(void);
void command_minus(void);
void command_minus_double(void);
void command_times(void);
void command_divide(void);
void command_modulo(void);
void command_dup(void);
void command_drop(void);
void command_swap(void);
void command_rot(void);
void command_over(void);
void command_1_minus(void);
void command_1_plus(void);
void command_help(void);
void command_decimal(void);
void command_hex(void);

void command_fetch(void);
void command_question(void);
void command_store(void);
void command_plus_store(void);
void command_cfetch(void);
void command_cquestion(void);
void command_cstore(void);
void command_2fetch(void);
void command_2question(void);
void command_2store(void);

void command_constants(void);
void command_variables(void);

void fatal_error(uint8_t);

// if you want help strings, define before including this file.

#ifdef HELP_STRINGS
# define HELP_STRING(x) x
#else
# define HELP_STRING(x) ""
#endif

#define COMMAND_STRINGS(NAME, WORD, HELP)	      \
 const char command_name_ ## NAME [] PROGMEM = WORD; \
 const char command_help_ ## NAME [] PROGMEM = HELP_STRING(HELP);

#define VARIABLE_STRINGS(NAME, WORD, HELP)	      \
 const char variable_name_ ## NAME [] PROGMEM = WORD; \
 const char variable_help_ ## NAME [] PROGMEM = HELP_STRING(HELP);

#define DICT_COMMAND_ENTRY(NAME) {command_name_ ## NAME, TYPE_COMMAND, {.command = command_ ## NAME}}
#define DICT_CONSTANT_ENTRY(NAME, VALUE) {constant_name_ ## NAME, TYPE_CONSTANT, {.constant = VALUE}}
#define DICT_VARIABLE_ENTRY(NAME, VARIABLE) {variable_name_ ## NAME, TYPE_VALUE, {.value = &VARIABLE}}  
#define DICT_CHAR_VARIABLE_ENTRY(NAME, VARIABLE) {variable_name_ ## NAME, TYPE_CVALUE, {.cvalue = &VARIABLE}}
#define DICT_DOUBLE_VARIABLE_ENTRY(NAME, VARIABLE) {variable_name_ ## NAME, TYPE_DVALUE, {.dvalue = &VARIABLE}}  

// consider having a compiler definition to just null out the help
#define HELP_COMMAND_ENTRY(NAME) {command_name_ ## NAME, command_help_ ## NAME}
#define HELP_VARIABLE_ENTRY(NAME) {variable_name_ ## NAME, variable_help_ ## NAME}
