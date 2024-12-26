#include <math.h> // pow
#include <stdbool.h> // bool
#include <stdio.h> // printf, fprintf, putchar, FILE, fgetc, EOF
#include <stdlib.h> // fopen, fclose
#include <string.h> // strlen, strcmp, memcpy
#include "include/arena.h" // arena
#include "include/array.h" // array
#include "include/types.h" // i32

// Tarzan is a tiny interpreted language with C-like syntax. This file includes a simple line-by-line interpreter.

// Global variables
u8 *file_data = 0; // File data
i64 file_size = 0; // File size
Arena *arena = 0; // Arena for memory allocation
Array *variables = 0; // Variables
i64 read_position = 0;

const i32 success = 0;
const i32 error = 1;

bool is_token(const char *token) {
  i32 token_length = strlen(token);
  if (read_position + token_length > file_size) return false;
  return memcmp(file_data + read_position, token, token_length) == 0;
}

// Number struct
typedef struct {
  i64 value;
  i16 exponent;
} Number;

// Variable struct
typedef struct {
  Number value;
  char *name;
} Variable;

typedef struct {
  u8 plus;
  u8 minus;
  u8 multiply;
  u8 divide;
} Operators;

const Operators operators = {
  .plus = 1,
  .minus = 2,
  .multiply = 3,
  .divide = 4
};

// Parse a number from the file data
Number parse_number() {
  Number number = {.value = 0, .exponent = 0};
  // Check if the number is negative
  bool negative = file_data[read_position] == '-';
  read_position += negative ? 1 : 0;

  bool decimal = false;
  while ((file_data[read_position] >= '0' && file_data[read_position] <= '9') || file_data[read_position] == '.') {
    if (file_data[read_position] == '.') {
      decimal = true;
    } else {
      number.value = number.value * 10 + file_data[read_position] - '0';
      if (decimal) {
        number.exponent -= 1;
      }
    }
    read_position += 1;
  }
  if (negative) {
    number.value = -number.value;
  }
  return number;
}

// Parses out a variable name and returns its value
Number parse_get_variable() {
  printf("parse_get_variable\n");
  i32 name_length = 0;
  // Store a pointer to the start of the variable name
  u8 *name_start = &file_data[read_position];
  while ((file_data[read_position] >= 'a' && file_data[read_position] <= 'z') || file_data[read_position] == '_') {
    name_length += 1;
    read_position += 1;
  }
  // Allocate memory for the variable name and copy it
  char *variable_name = malloc(sizeof(char) * (name_length + 1));
  if (variable_name == NULL) {
    fprintf(stderr, "Memory allocation failed\n");
    exit(1);
  }
  memcpy(variable_name, name_start, name_length);
  // Null terminate the string
  variable_name[name_length] = '\0';

  Number value = {.value = 0, .exponent = 0};
  bool value_found = false;
  i32 iterator = array_length(variables) - 1;
  // Search for the variable in the variables array starting from the most recently added variables
  while (iterator >= 0 && !value_found) {
    Variable *variable = (Variable *)array_get(variables, iterator);
    if (strcmp(variable->name, variable_name) == 0) {
      value = variable->value;
      value_found = true;
    }
    iterator -= 1;
  }
  free(variable_name);
  return value;
}

// Takes two numbers and aligns them at the lowest exponent
void align_exponents(Number *a, Number *b) {
  if (a->exponent < b->exponent) {
    b->value *= pow(10, b->exponent - a->exponent);
    b->exponent = a->exponent;
  } else if (a->exponent > b->exponent) {
    a->value *= pow(10, a->exponent - b->exponent);
    a->exponent = b->exponent;
  }
}

// Takes a number and adds a number of decimals to it
void add_decimals(Number *number, i32 decimals) {
  number->value *= pow(10, decimals);
  number->exponent -= decimals;
}

Number divide_numbers(Number *a, Number *b) {
  Number result = {.value = 0, .exponent = 0};
  if (b->value != 0) {
    // Add 3 decimals
    add_decimals(a, 3 + abs(a->exponent));
    result.value = a->value / b->value;
    result.exponent = a->exponent - b->exponent;
  }
  return result;
}

Number compact_number(Number number) {
  Number result = number;
  while (result.value % 10 == 0 && result.value != 0) {
    result.value /= 10;
    result.exponent += 1;
  }
  return result;
}

// Evaluate an expression
Number evaluate_expression() {
  Number result = {.value = 0, .exponent = 0};
  u8 parsed_numbers = 0; // Keep track of current parser window
  Number first_number = {.value = 0, .exponent = 0};
  Number second_number = {.value = 0, .exponent = 0};
  Number third_number = {.value = 0, .exponent = 0}; // For the case of a + b * c, we want to wait with the addition until we know the result of the multiplication
  u8 op_code = 0; // 1 = plus, 2 = minus, 3 = multiply, 4 = divide
  u8 op_code2 = 0;
  // Read the first number
  while (!is_token(")") && !is_token(";") && read_position < file_size) {
    while (is_token(" ")) {
      read_position += 1;
    }
    // If operator
    if (is_token("+")) {
      read_position += 1;
      op_code = op_code == 0 ? operators.plus : op_code;
      op_code2 = op_code > 0 ? operators.plus : 0;
    } else if (is_token("-")) {
      read_position += 1;
      op_code = op_code == 0 ? operators.minus : op_code;
      op_code2 = op_code > 0 ? operators.minus : 0;
    } else if (is_token("*")) {
      read_position += 1;
      op_code = op_code == 0 ? operators.multiply : op_code;
      op_code2 = op_code > 0 ? operators.multiply : 0;
    } else if (is_token("/")) {
      read_position += 1;
      op_code = op_code == 0 ? operators.divide : op_code;
      op_code2 = op_code > 0 ? operators.divide : 0;
    } else if (file_data[read_position] >= '0' && file_data[read_position] <= '9') {
      Number number = parse_number();
      if (parsed_numbers == 0) {
        first_number = number;
        parsed_numbers += 1;
      } else if (parsed_numbers == 1) {
        second_number = number;
        parsed_numbers += 1;
      } else {
        third_number = number;
        parsed_numbers += 1;
      }
    }
    // if variable name
    else if (file_data[read_position] >= 'a' && file_data[read_position] <= 'z') {
      printf("found variable\n");
      Number variable = parse_get_variable();
      if (parsed_numbers == 0) {
        first_number = variable;
        parsed_numbers += 1;
      } else if (parsed_numbers == 1) {
        second_number = variable;
        parsed_numbers += 1;
      } else {
        third_number = variable;
        parsed_numbers += 1;
      }
    }
    // if new block
    else if (is_token("(")) {
      printf("found (\n");
      read_position += 1;
      Number block_result = evaluate_expression();
      read_position += 1;
      if (parsed_numbers == 0) {
        first_number = block_result;
        parsed_numbers += 1;
      } else if (parsed_numbers == 1) {
        second_number = block_result;
        parsed_numbers += 1;
      } else {
        third_number = block_result;
        parsed_numbers += 1;
      }
    } else {
      printf("Unknown token in eval\n");
    }
    // Run calculation if we have three numbers, to make room for the next number
    if (parsed_numbers == 3) {
      // If second operator has higher priority, run it first
      if (op_code2 == operators.multiply || op_code2 == operators.divide) {
        align_exponents(&second_number, &third_number);
        if (op_code2 == operators.multiply) {
          second_number.value *= third_number.value;
          second_number.exponent += third_number.exponent;
        } else if (op_code2 == operators.divide) {
          second_number = divide_numbers(&second_number, &third_number);
        }
      }
      // Run only the first operator and move the third operator
      else {
        align_exponents(&first_number, &second_number);
        if (op_code == operators.plus) {
          first_number.value += second_number.value;
        } else if (op_code == operators.minus) {
          first_number.value -= second_number.value;
        } else if (op_code == operators.multiply) {
          first_number.value *= second_number.value;
          first_number.exponent += second_number.exponent;
        } else if (op_code == operators.divide) {
          first_number = divide_numbers(&first_number, &second_number);
        }
        // Move third number to second number slot
        second_number = third_number;
        op_code = op_code2;
      }
      // Reset third number
      third_number = (Number){.value = 0, .exponent = 0};
      op_code2 = 0;
      parsed_numbers = 2;
    }
  } // end while - has reached end of expression
  // If there are two numbers, run the operator
  if (parsed_numbers == 2) {
    align_exponents(&first_number, &second_number);
    if (op_code == operators.plus) {
      result.value = first_number.value + second_number.value;
    } else if (op_code == operators.minus) {
      result.value = first_number.value - second_number.value;
    } else if (op_code == operators.multiply) {
      result.value = first_number.value * second_number.value;
      first_number.exponent += second_number.exponent;
    } else if (op_code == operators.divide) {
      result = divide_numbers(&first_number, &second_number);
      first_number.exponent = result.exponent;
    }
    result.exponent = first_number.exponent;
  }
  // If there is only one number, return it
  else if (parsed_numbers == 1) {
    result = first_number;
  }
  // Compact the result number by removing trailing zeros
  result = compact_number(result);
  printf("evaluate_expression result: %lld * 10^%d\n", result.value, result.exponent);
  return result;
}

// Parses a variable and sets it in the variables array
i64 parse_set_variable() {
  printf("parse_set_variable\n");

  // Skip spaces
  while (is_token(" ")) {
    read_position += 1;
  }

  // Get the variable name start and length
  u8 *name_start = &file_data[read_position];
  i32 name_length = 0;
  while ((file_data[read_position] >= 'a' && file_data[read_position] <= 'z') || file_data[read_position] == '_') {
    name_length += 1;
    read_position += 1;
  }

  // Allocate memory for the variable name and copy it
  char *variable_name = arena_fill(arena, sizeof(char) * (name_length + 1));
  if (variable_name == NULL) {
    printf("Memory allocation failed in set_variable\n");
    return error;
  }
  memcpy(variable_name, name_start, name_length);
  variable_name[name_length] = '\0'; // Null terminate the string

  // Skip spaces and =
  while (is_token(" ") || is_token("=")) {
    read_position += 1;
  }
  // Get the value
  Number value = evaluate_expression();

  // Push the variable to the variables array
  Variable new_variable = {
    .value = value,
    .name = variable_name
  };
  array_push(variables, &new_variable);

  return success;
}

i64 enter_block() {
  printf("enter_block\n");
  while (!is_token("{") && read_position < file_size) {
    read_position += 1;
  }
  // Step over the {
  read_position += 1;
  return success;
}

i64 skip_block() {
  printf("skip_block\n");
  i32 block_count = 1;
  while (block_count > 0 && read_position < file_size) {
    if (is_token("{")) {
      block_count += 1;
    } else if (is_token("}")) {
      block_count -= 1;
    }
    read_position += 1;
  }
  return success;
}

i64 skip_line() {
  printf("skip_line\n");
  while (!is_token("\n") && read_position < file_size) {
    read_position += 1;
  }
  // Step over the \n
  read_position += 1;
  return success;
}

// Assignment or function call
i64 handle_other() {
  if (is_token("var")) {
    printf("new variable\n");
    read_position += 3;
    // Read the variable name and value and add it to the variables array
    parse_set_variable();
  } else {
    printf("Unknown token\n");
    read_position += 1;
  }
  return success;
}

// Recursive function that reads at a given position and returns the new position
i32 parse_token() {
  // Skip spaces
  while (is_token(" ") || is_token("\n")) {
    read_position += 1;
    printf("space or newline\n");
  }
  if (is_token("(")) {
    printf("start paren\n");
    read_position += 1;
    Number result = evaluate_expression();
    printf("result: %lld * 10^%d\n", result.value, result.exponent);
    read_position += 1;
  } else if (is_token(")")) {
    read_position += 1;
    printf("end paren\n");
  } else if (is_token("}")) {
    read_position += 1;
    printf("end block\n");
  } else if (is_token("if")) {
    printf("if\n");
    read_position += 2;
    enter_block();
    skip_block();
  } else if (is_token("else")) {
    printf("else\n");
    read_position += 4;
    enter_block();
    skip_block();
  } else if (is_token("while")) {
    printf("while\n");
    read_position += 5;
  } else if (is_token("//")) {
    printf("comment\n");
    skip_line();
  } else {
    handle_other();
  }
  return success;
}

i32 main(i32 arg_count, char *arguments[]) {
  if (arg_count != 2) {
    fprintf(stderr, "Tarzan wants: %s <filename>\n", arguments[0]);
    return 1;
  }

  FILE *file = fopen(arguments[1], "r");
  if (file == NULL) {
    fprintf(stderr, "Tarzan can't open file %s\n", arguments[1]);
    return 1;
  }

  // Copy the file into memory
  fseek(file, 0, SEEK_END);
  file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  arena = arena_open(file_size);
  file_data = (u8 *)arena_fill(arena, file_size);
  fread(file_data, 1, file_size, file);

  variables = array_create(arena, sizeof(Variable));

  // Parse the file
  while (read_position < file_size) {
    parse_token();
  }
  printf("Tarzan done!\n");
  arena_close(arena);
  fclose(file);
  return 0;
}