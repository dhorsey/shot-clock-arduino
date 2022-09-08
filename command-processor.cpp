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

#include <avr/wdt.h>
#include <Arduino.h>
#include "command-processor.h"

char input_buf[INPUT_BUFFER_SIZE];
char output_buf[OUTPUT_BUFFER_SIZE];
char buf_counter = 0;
char *g_tokens[MAX_TOKENS];
int16_t g_data_stack[DATA_STACK_MAX+1];
uint8_t g_data_stack_size = 0;

uint8_t g_base = 10;
uint16_t g_x = 1971; // test variable

/* This is how we have to separate the strings to put them into flash memory in the dictionary */
const char format_str_decimal[] PROGMEM = "%d ";
const char format_str_hex[] PROGMEM = "%x ";
const char format_str_decimal_double[] PROGMEM = "%ld";
const char format_str_hex_double[] PROGMEM = "%08x";

COMMAND_STRINGS(print_data_stack, ".s", "");
COMMAND_STRINGS(pop, ".", "");
COMMAND_STRINGS(pop_double, "d.", "");
COMMAND_STRINGS(pop_decimal, ".d", "");
COMMAND_STRINGS(words, "words", "");
COMMAND_STRINGS(plus, "+", "");
COMMAND_STRINGS(plus_double, "d+", "");
COMMAND_STRINGS(minus, "-", "");
COMMAND_STRINGS(minus_double, "d-", "");
COMMAND_STRINGS(times, "*", "");
COMMAND_STRINGS(divide, "/", "");
COMMAND_STRINGS(modulo, "%", "");
COMMAND_STRINGS(dup, "dup", "");
COMMAND_STRINGS(drop, "drop", "");
COMMAND_STRINGS(swap, "swap", "");
COMMAND_STRINGS(rot, "rot", "");
COMMAND_STRINGS(over, "over", "");
COMMAND_STRINGS(1_minus, "1-", "");
COMMAND_STRINGS(1_plus, "1+", "");
COMMAND_STRINGS(fetch, "@", ""); 
COMMAND_STRINGS(question, "?", ""); 
COMMAND_STRINGS(store, "!", ""); 
COMMAND_STRINGS(plus_store, "+!", ""); // p. 190  (n adr -- )
COMMAND_STRINGS(cfetch, "c@", ""); 
COMMAND_STRINGS(cquestion, "c?", ""); 
COMMAND_STRINGS(cstore, "c!", ""); 
COMMAND_STRINGS(2fetch, "2@", ""); 
COMMAND_STRINGS(2question, "2?", ""); 
COMMAND_STRINGS(2store, "2!", ""); 
COMMAND_STRINGS(constants, "constants", ""); 
COMMAND_STRINGS(variables, "vars", ""); 
COMMAND_STRINGS(help, "help", "Print out this help message");
COMMAND_STRINGS(decimal, "decimal", "Switch to base 10");
COMMAND_STRINGS(hex, "hex","Switch to base 16");

const char constant_name_pi[] PROGMEM = "pi";
const char variable_name_base[] PROGMEM = "base";
const char variable_name_x[] PROGMEM = "x";// test 16-bit variable

const struct dictionary_entry g_base_dictionary[] PROGMEM =
  {
   // C99 "designated initializers"
   DICT_COMMAND_ENTRY(print_data_stack),
   DICT_COMMAND_ENTRY(pop),
   DICT_COMMAND_ENTRY(pop_double),
   DICT_COMMAND_ENTRY(words),
   DICT_COMMAND_ENTRY(plus),
   DICT_COMMAND_ENTRY(plus_double),
   DICT_COMMAND_ENTRY(minus),
   DICT_COMMAND_ENTRY(minus_double),
   DICT_COMMAND_ENTRY(minus),
   DICT_COMMAND_ENTRY(times),
   DICT_COMMAND_ENTRY(divide),
   DICT_COMMAND_ENTRY(modulo),
   DICT_COMMAND_ENTRY(dup),
   DICT_COMMAND_ENTRY(drop),
   DICT_COMMAND_ENTRY(swap),
   DICT_COMMAND_ENTRY(rot),
   DICT_COMMAND_ENTRY(over),
   DICT_COMMAND_ENTRY(1_minus),
   DICT_COMMAND_ENTRY(1_plus),
   DICT_COMMAND_ENTRY(help),
   DICT_COMMAND_ENTRY(hex),
   DICT_COMMAND_ENTRY(decimal),
   DICT_COMMAND_ENTRY(constants),
   DICT_COMMAND_ENTRY(variables),
   DICT_COMMAND_ENTRY(fetch),
   DICT_COMMAND_ENTRY(question),
   DICT_COMMAND_ENTRY(store),
   DICT_COMMAND_ENTRY(plus_store),
   DICT_COMMAND_ENTRY(cfetch),
   DICT_COMMAND_ENTRY(cquestion),
   DICT_COMMAND_ENTRY(cstore),
   DICT_COMMAND_ENTRY(2fetch),
   DICT_COMMAND_ENTRY(2question),
   DICT_COMMAND_ENTRY(2store),
   DICT_CHAR_VARIABLE_ENTRY(base, g_base),
   DICT_CONSTANT_ENTRY(pi, 31415),
   DICT_VARIABLE_ENTRY(x, g_x), // test integer variable
   {NULL,                          TYPE_END_OF_DICT, NULL}
  };
  
const struct help_entry g_base_help[] PROGMEM =
  {
   HELP_COMMAND_ENTRY(help),
   HELP_COMMAND_ENTRY(hex),
   HELP_COMMAND_ENTRY(decimal),
   {NULL, NULL} // end-of-dictionary sentinel
  };

/* These must be defined in PROGMEM in the application; they can be defined to be NULL */
struct dictionary_entry *get_application_dictionary();
extern void application_rc_printer(uint8_t rc);
extern struct help_entry *get_application_help();

void process_serial_input() {

  if (Serial.available() > 0) {  /* Arduino serial buffer is 64 bytes */
    // read the incoming byte:
    int16_t incoming = Serial.read();

    if (incoming != -1) {

      // wrap around the buffer
      if (buf_counter >= INPUT_BUFFER_SIZE) {
	buf_counter = 0;
      }
      
      if (incoming == '\n') {
	input_buf[buf_counter] = '\0';
	command_interpret();
	buf_counter = 0;
      } else {
	input_buf[buf_counter] = (char)incoming;
	buf_counter++;
      }
    }
  }
}

int16_t tokenize_input_buf() {
  int16_t token_count = 0;

  /* strtok replaces delimeters with string terminators. */
  char *ptr = strtok(input_buf, DELIMETERS); /* *last; *last would be for strtok_r, the reentrant form of strtok */
  while((ptr != NULL) && (token_count < MAX_TOKENS)) {
    g_tokens[token_count++] = ptr;
    ptr = strtok(NULL, DELIMETERS);
  }
  return token_count;
}

void print_flash_string(const char* s_flash) { // output a flash string to Serial
  if (!s_flash) 
    return;
  char c;
  while ((c = pgm_read_byte(s_flash++)))
    Serial.print(c);
}

size_t find_max_name_length(struct help_entry *he_flash) {
  size_t max_name_length = 0;

  if(he_flash) {
    struct help_entry he;
    while(1) {
      memcpy_P(&he, he_flash, sizeof(struct help_entry));
      if(he.name == NULL) {
	break;
      } else {
	if(he.help) {
	  size_t len = strlen_P(he.name);
	  max_name_length = len > max_name_length ? len : max_name_length;
	}
	he_flash++;
      }
    }
  }
  return max_name_length;
}

size_t find_max_name_length_all_help() {
  return max(find_max_name_length(g_base_help),
	     find_max_name_length(get_application_help()));
}

void print_command_help(struct help_entry *h, uint8_t left_margin) {
  wdt_reset();
  
  print_flash_string(h->name);
  size_t current_pos = strlen_P(h->name);
  while (current_pos++ <= left_margin) {
    Serial.print(F(" "));
  }
  print_flash_string(h->help);
  Serial.println();
}

void print_command_help_one_dictionary(struct help_entry *he_flash,
				       uint8_t left_margin) {
  if(he_flash != NULL) {
    struct help_entry h;
    while(1) {
      memcpy_P(&h, he_flash, sizeof(struct help_entry));
      if (h.name == NULL) {
	break;
      } else {
	print_command_help(&h, left_margin);
	he_flash++;
      }
    }
  }
}

void command_help() {
  uint16_t max_name_len = find_max_name_length_all_help();
  print_command_help_one_dictionary(g_base_help, max_name_len + 2);
  print_command_help_one_dictionary(get_application_help(), max_name_len + 2);
}

void output_dictionary_words(struct dictionary_entry *de_flash, char type) {
  if(!de_flash)
    return;

  struct dictionary_entry de;
  while(1) {
    /*
      Serial.print(F("WORDS DEBUG name="));
      print_flash_string(de.name);
      Serial.print(F("type="));
      Serial.println(de.type);
    */
    
    memcpy_P(&de, de_flash, sizeof(struct dictionary_entry));
    de_flash++; // advance to next entry

    if(de.type == TYPE_END_OF_DICT)
      return;
    else if(de.type == type) {
      print_flash_string(de.name);
      Serial.print(F(" "));
    }
  }
}

void output_words(char type) {
  output_dictionary_words(get_application_dictionary(), type);
  output_dictionary_words(g_base_dictionary, type);
  Serial.println(F(" "));
}

void command_words() {
  output_words(TYPE_COMMAND);
}

void command_variables() {
  Serial.print(F("8-bit variables: "));
  output_words(TYPE_CVALUE);
  Serial.print(F("16-bit variables: "));
  output_words(TYPE_VALUE);
  Serial.print(F("32-bit variables: "));
  output_words(TYPE_DVALUE);
}

void command_constants() {
  output_words(TYPE_CONSTANT);
}


/* If I want to keep the same code, then I need to pass in the size of the dictionary
   entries, and do a bunch of non-pointer safe stuff. 

   That's what led to C++ type parameterization: you don't want to write out the same
   code yourself for each type.   My objection there is that you may as well see the
   generated code, by generating it yourself.  Otherwise, write weird code with a
   bunch (void*).  Unit tests help maintain the extra code, and you'd need these for
   the different types, although it would be easier.  

   One way around it is to have a common dictionary header, and then define the
   types.  

   However, if you just write similar code for different types, you have flexibility
   and type-safety, at the expense of more re-work if you refactor.  
*/

uint8_t lookup_entry(struct dictionary_entry *de_flash, const char* name,
		     struct dictionary_entry **de_flash_found,
		     struct dictionary_entry *found) {

  uint8_t rc = ERROR_WORD_NOT_FOUND;
  if(de_flash == NULL) {
    return rc;
  }

  while(1) {
    memcpy_P(found, de_flash, sizeof(struct dictionary_entry));

    if(found->name == NULL) {
      break;
    }

    /*
      Serial.print(F("DEBUG NAME: "));
      print_flash_string(found->name);
      Serial.println();
    */
    
    if(strcmp_P(name, found->name) == 0) {
      /*
	Serial.print(F("DEBUG FOUND NAME: "));
	print_flash_string(found->name);
	Serial.println();
      */
      
      rc = SUCCESS;
      *de_flash_found = de_flash;
      break;
    } else {
      de_flash++;
    }
  }

  /*
    Serial.print(F("LOOKUP_ENTRY: "));
    print_flash_string(found->name);
    Serial.print(F(" rc="));
    Serial.println(rc);
  */
  
  return rc;
}


uint8_t find_dictionary_entry(const char* name, struct dictionary_entry **de_flash_found, struct dictionary_entry *found) {

  // **de_flash_found is space where we can write out the flash pointer to the dictionary entry
  // *found is space where we can write a copy of the dictionary entry

  uint8_t rc = ERROR_WORD_NOT_FOUND;
  rc = lookup_entry(get_application_dictionary(), name, de_flash_found, found);
  if (rc != SUCCESS) {
    rc = lookup_entry(g_base_dictionary, name, de_flash_found, found);
  }

  // Serial.print(F("FIND_DICTIONARY_ENTRY: rc="));
  // Serial.println(rc);
  return rc;
}

void print_rc(uint8_t rc) {
  switch (rc) {
  case(SUCCESS): 
    Serial.println(F(" ok"));
    break;
  case(ERROR):
    Serial.println(F("ERROR"));
    break;
  case (ERROR_WORD_NOT_FOUND):
    Serial.println(F("ERROR: Word not found")); // maybe set a pointer to the word in the input buffer
    break;
  default:
    application_rc_printer(rc);
    break;
  }
}

void fatal_error(uint8_t rc) {
  switch (rc) {
  case (ERROR_STACK_OVERFLOW):
    Serial.println(F("FATAL ERROR: Stack overflow"));
    break;
  case (ERROR_STACK_UNDERFLOW):
    Serial.println(F("FATAL ERROR: Stack underflow"));
    break;
  case (ERROR_UNKNOWN_TYPE):
    Serial.println(F("FATAL ERROR: unknown fatal error"));
  default:
    Serial.print(F("FATAL ERROR: error "));
    Serial.println(rc);
    break;
  }
  Serial.println(F("Triggering watchdog reset"));
  while(1) {
  }
}


bool is_double(const char *s, int len) {
  /* e.g. 200,000 is a double wide (32-bit) integer */
  bool comma_test = false;

  for (int i=0; i<len; i++) {
    char c = s[i];

    if((c == '-') && (i == 0)) {
      continue;
    }
    
    if (c == ',') {
      comma_test = true;
    } else {
      bool digit_test = (g_base == 16 ? isHexadecimalDigit(c) : isDigit(c));
      if(!digit_test)
	return false;
    }
  }
  /*
    Serial.println(F("IS_SINGLE: "));
    Serial.println(s);
  */
  return comma_test;
}

bool is_single(const char *s, int len) {
  for (int i=0; i<len; i++) {
    char c = s[i];
    
    if((c == '-') && (i == 0)) {
      continue;
    }
    
    bool test = (g_base == 16 ? isHexadecimalDigit(s[i]) : isDigit(s[i]));
    if(!test)
      return false;
  }
  return true;
}

int16_t parse_single(const char *token, int len) {
  int32_t double_wide;
  double_wide= strtol(token, NULL, g_base == 16 ? 16 : 10);
  return (int16_t) double_wide;
}

int32_t parse_double(char *token, int len) {
  // we will remove the commas from the token, changing it in the input buffer
  char *src = token;
  char *dst = token;
  
  while(char c = *src) {
    if (c != ',') {
      *dst = c;
      dst++;
    }
    src++;
  }
  *dst = '\0';

  return strtol(token, NULL, g_base == 16 ? 16 : 10);
}

void push_single(int16_t number) {
  // Serial.print(F("PUSH: "));
  // Serial.println(number);
  
  if (g_data_stack_size >= DATA_STACK_MAX) {
    fatal_error(ERROR_STACK_OVERFLOW);
  }

  g_data_stack[g_data_stack_size++] = number;
}

void push_two_singles(int16_t n1, int16_t n2) {
  push_single(n1);
  push_single(n2);
}

void push_four_singles(int16_t n1, int16_t n2, int16_t n3, int16_t n4) {
  push_single(n1);
  push_single(n2);
  push_single(n3);
  push_single(n4);
}

void push_double(int32_t number) {
  if (g_data_stack_size >= DATA_STACK_MAX-1) {
    fatal_error(ERROR_STACK_OVERFLOW);
  }
  
  union double_word d = { .signed_double_word = number };
  uint16_t high_word = d.unsigned_word[1];
  uint16_t low_word = d.unsigned_word[0];

  /*
    Serial.print(F(" number="));
    Serial.println(number, HEX);
    Serial.print(F(" signed_double_word="));
    Serial.println(d.signed_double_word, HEX);
    Serial.print(F("high_word="));
    Serial.print(high_word, HEX);
    Serial.print(F(" low_word="));
    Serial.println(low_word, HEX);
  */

  g_data_stack[g_data_stack_size++] = high_word;
  g_data_stack[g_data_stack_size++] = low_word;
}

int16_t pop_single() {
  // Serial.print(F("POP: "));
  
  if (g_data_stack_size < 1) {
    fatal_error(ERROR_STACK_UNDERFLOW);
  }

  // Serial.println(g_data_stack[g_data_stack_size-1]);

  return g_data_stack[--g_data_stack_size];
}

int32_t pop_double() {
  if (g_data_stack_size < 2) {
    fatal_error(ERROR_STACK_UNDERFLOW);
  }
  
  union double_word d;
  uint16_t low_word = g_data_stack[--g_data_stack_size];
  uint16_t high_word = g_data_stack[--g_data_stack_size];
  d.unsigned_word[1] = high_word;
  d.unsigned_word[0] = low_word;
  return d.signed_double_word;

  /*
    Serial.print(F("high_word="));
    Serial.print(high_word, HEX);
    Serial.print(F(" low_word="));
    Serial.print(low_word, HEX);
    Serial.print(F(" result="));
    Serial.println(*number, HEX);
  */
}

void command_print_data_stack() {
  const char *format_str = g_base == 16 ? format_str_hex : format_str_decimal;
  for(int i=0; i<g_data_stack_size; i++) {
    sprintf_P(output_buf, format_str, g_data_stack[i]);
    Serial.print(output_buf);
  }
}

void print_single(int16_t number) {
  const char *format_str = g_base == 16 ? format_str_hex : format_str_decimal;
  sprintf_P(output_buf, format_str, number);
  Serial.print(output_buf);
}

void command_pop() {
  int16_t number = pop_single();
  print_single(number);
}

void print_double(int32_t number) {
  const char *format_str = g_base == 16 ? format_str_hex_double : format_str_decimal_double;
  sprintf_P(output_buf, format_str, number);

  int len = strlen(output_buf);
  if(len > 0) {
    char c = output_buf[0];
    char *src=output_buf;

    if (c == '-') {
      Serial.print(c);
      src++;
      len--;
      // we run the same algorithm for printing commas, just skip the first char.
    }
      
    for(int i = 0; i<len; i++) {
      c = src[i];
      if(g_base == 10) {
	int digits_before_comma = (len-i) % 3;
	if((i > 0) && (digits_before_comma == 0)) {
	  Serial.print(',');
	}
      }
      Serial.print(c);
    }
  }
}

void command_pop_double() {
  int32_t number = pop_double();
  print_double(number);
  Serial.println(' ');
}

void command_fetch() {
  int16_t pvalue = pop_single();
  // Serial.print(F("address=0x"));
  // Serial.println(pvalue, HEX);

  // TODO: Check if this is a known variable.  Throw exception if not.
  // TODO: add some intelligence so I don't need c@, etc. for fetching byte variables.
  
  int16_t value = *(int16_t *)pvalue;
  // Serial.print(F("value="));
  //  Serial.println(value);
  push_single(value);
}

void command_question() {
  command_fetch();
  command_pop();
}

void command_store() {
  // (n adr -- )
  int16_t pvalue = pop_single();
  int16_t value = pop_single();
  // TODO: consider checking if value is a valid variable
  *(int16_t *)pvalue = value;
}

void command_plus_store() {
  // (n adr -- )
  // : plus_store swap over @ + swap ! ;
  command_swap();
  command_over();
  command_fetch();
  command_plus();
  command_swap();
  command_store();
}

void command_cfetch() {
  int16_t pvalue = pop_single();
  // Serial.print(F("address=0x"));
  // Serial.println(pvalue, HEX);
  int8_t value = *(int8_t *)pvalue;
  // Serial.print(F("value="));
  // Serial.println(value);
  push_single(value);
}

void command_cquestion() {
  command_cfetch();
  command_pop(); // nothing special; chars still take up a cell on the stack
}

void command_cstore() {
  // top of stack is location
  int16_t pvalue = pop_single();
  int16_t value = pop_single();
  *((int8_t *)pvalue) = (int8_t)value;
}

void command_2fetch() {
  int16_t pvalue = pop_single();

  // Serial.print(F("address=0x"));
  // Serial.println(pvalue, HEX);

  int32_t value = *((int32_t *)pvalue);

  // Serial.print(F("2value="));
  // Serial.println(value);

  push_double(value);
}

void command_2question() {
  command_2fetch();
  command_pop_double();
}

void command_2store() {
  // top of stack is location
  int16_t pvalue = pop_single();
  int32_t value = pop_double();  
  *(int32_t *)pvalue = value; // store the value
  // any bounds checking here should call fatal_error() if a problem
}

void command_plus() {
  int16_t y = pop_single();
  int16_t x = pop_single();
  push_single(x+y);
}

void command_plus_double() {
  int32_t y = pop_double();
  int32_t x = pop_double();
  push_double(x+y);
}

void command_minus() {
  int16_t y = pop_single();
  int16_t x = pop_single();
  push_single(x-y);
}

void command_minus_double() {
  int32_t y = pop_double();
  int32_t x = pop_double();
  push_double(x-y);
}

void command_times() {
  int16_t y = pop_single();
  int16_t x = pop_single();
  push_single(x*y);
}

void command_divide() {
  int16_t y = pop_single();
  int16_t x = pop_single();
  push_single(x/y);
}

void command_modulo() {
  int16_t y = pop_single();
  int16_t x = pop_single();
  push_single(x%y);
}

void command_dup() {
  if(g_data_stack_size < 1)
    fatal_error(ERROR_STACK_UNDERFLOW);
  push_single(g_data_stack[g_data_stack_size-1]);
}

void command_drop() {
  pop_single();
}

// swap and rot do not change the stack size, so do in place.
void command_swap() {
  int16_t temp;
  if(g_data_stack_size < 2)
    fatal_error(ERROR_STACK_UNDERFLOW);
  else {
    uint8_t tos = g_data_stack_size -1;
    temp=g_data_stack[tos];
    g_data_stack[tos] = g_data_stack[tos-1];
    g_data_stack[tos-1] = temp;
  }
}

void command_rot() {
  if(g_data_stack_size < 3)
    fatal_error(ERROR_STACK_UNDERFLOW);
  else {
    int16_t temp;
    uint8_t tos = g_data_stack_size -1;
    temp=g_data_stack[tos];
    g_data_stack[tos] = g_data_stack[tos-1];
    g_data_stack[tos-1] = g_data_stack[tos-2];
    g_data_stack[tos-2] = temp;
  }
}

void command_over() {
  if(g_data_stack_size < 2)
    fatal_error(ERROR_STACK_UNDERFLOW);
  else {
    push_single(g_data_stack[g_data_stack_size-2]);
  }
}

void command_1_minus() {
  // don't require any stack space
  if(g_data_stack_size < 1)
    fatal_error(ERROR_STACK_UNDERFLOW);
  else {
    g_data_stack[g_data_stack_size-1]--;
  }
}

void command_1_plus() {
  // don't require any stack space
  if(g_data_stack_size < 1)
    fatal_error(ERROR_STACK_UNDERFLOW);
  else {
    g_data_stack[g_data_stack_size-1]++;
  }
}

void command_decimal() {
  g_base = 10;
}

void command_hex() {
  g_base = 16;
}

void execute_dictionary_command(struct dictionary_entry *pde) {
  /*
    Serial.print(F("EXECUTING: "));
    print_flash_string(pde->name);
    Serial.println();
  */

  (pde->cell.command)();
}

void command_processor_handle_error(uint8_t rc) {
  // Mr. Moore says to blow away the data on the stack if there are any errors.
  g_data_stack_size = 0;
  print_rc(rc);
}

void command_interpret() {
  uint8_t rc = SUCCESS;

  /*
    Serial.println(F("INTERPRET"));
    Serial.print(F("INPUT_BUF: "));
    Serial.println(input_buf);
  */

  int16_t token_count = tokenize_input_buf();

  // echo back the input  
  for (int16_t i = 0; i < token_count; i++) {
    Serial.print(g_tokens[i]);
    Serial.print(F(" "));
  }
  Serial.println();

  // Serial.println(F("NOW PARSING"));

  // Parse the input.  If a number, put it on the data stack.
  // if a word, execute the word.

  for(int16_t i = 0; i<token_count; i++) {
    char *token = g_tokens[i];
    int16_t len=strlen(token);
    if(is_single(token, len)) {
      int16_t single = parse_single(token, len);
      push_single(single);
    } else if (is_double(token, len)) {
      uint32_t double_wide = parse_double(token, len);
      push_double(double_wide);
    } else {
      /* Serial.print(F("TOKEN: "));
	 Serial.println(token); */

      // for now, let's allow quoting a single char, and put the ascii value
      // on the stack.  there might be a more forthy way to do this
      // TODO: change to use CHAR.  This means the commands needs access to the input token stream */
      if ((len == 2) && (token[0] == '\'')) {
	/* Serial.print(F("CHARACTER: "));
	   Serial.println(token[1]); */
	push_single(token[1]);
      } else {
	struct dictionary_entry *de_flash_found;
	struct dictionary_entry found;
	rc = find_dictionary_entry(token, &de_flash_found, &found);
	if (rc == SUCCESS) {
	  switch(found.type) {
	  case TYPE_COMMAND:
	    execute_dictionary_command(&found);
	    break;
	  case TYPE_CONSTANT:
	    push_single((int16_t) found.cell.constant);
	    break;
	  case TYPE_CVALUE:
	  case TYPE_VALUE:
	  case TYPE_DVALUE:
	    // for all variable types, push a pointer to the value onto the stack.
	    // You have to know the type of the variable to fetch it properly.
	    push_single((int16_t) found.cell.value);
	    break;
	  default:
	    fatal_error(ERROR_UNKNOWN_TYPE);
	    break;
	  }
	} else {
	  command_processor_handle_error(rc);
	}
      }
    }
  }
  Serial.println(F("ok"));
}

