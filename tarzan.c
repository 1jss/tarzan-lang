#include <math.h> // pow
#include <stdbool.h> // bool
#include <stdio.h> // printf, FILE
#include <stdlib.h> // fopen, fclose
#include <string.h> // strlen, strcmp, memcpy
#include <time.h> // clock, CLOCKS_PER_SEC

#include "include/types.c" // i32
#include "include/arena.c" // arena
#include "include/array.c" // array

// Tarzan is a tiny interpreted language with C-like syntax. This file includes a simple line-by-line interpreter.

// TODO:
// - [x] Add if
// - [x] Add logical condition evaluation
// - [x] Add else
// - [x] Add while
// - [x] Assignment of existing variable
// - [x] Add print statement
// - [x] Add snippet declaration
// - [x] Add insert snippet
// - [ ] Add string type
// - [ ] Add undefined Number value
// - [x] Add scope to variables by block level, removing current block level variables on block end

// Global variables
u8 *file_data = 0; // File data
i64 file_size = 0; // File size
Arena *arena = 0; // Arena for memory allocation
Array *variables = 0; // Variables
Array *snippets = 0; // Snippets
i64 read_position = 0;
Array *jump_stack = 0; // Jump stack determines what happens when a block ends
i32 block_level = 0; // Current block level used for variable scope

const i32 success = 0;
const i32 error = 1;

bool is_token(const char *token) {
  i32 token_length = strlen(token);
  if (read_position + token_length > file_size) return false;
  return memcmp(file_data + read_position, token, token_length) == 0;
}

typedef struct {
  u8 skip_else; // if-else blocks
  u8 return_to; // while blocks and function calls
} JumpTypes;

// Block ends for different block types
const JumpTypes jumps = {
  .skip_else = 1,
  .return_to = 2,
};

// Block types for the block stack
typedef struct {
  u8 type; // jump type
  i64 index; // stored read position
} Jump;

Jump skip_else_jump = {
  .type = 1,
  .index = 0
};

// Number struct
typedef struct {
  i64 value;
  i16 exponent;
} Number;

// Variable struct
typedef struct {
  Number value;
  char *name;
  i32 level;
} Variable;

// Snippet struct
typedef struct {
  char *name;
  i64 index;
} Snippet;

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

typedef struct {
  u8 equal_to;
  u8 less_than;
  u8 greater_than;
  u8 less_than_or_equal_to;
  u8 greater_than_or_equal_to;
} Comparators;

const Comparators comparators = {
  .equal_to = 1,
  .less_than = 2,
  .greater_than = 3,
  .less_than_or_equal_to = 4,
  .greater_than_or_equal_to = 5
};

// Parse a number
Number parse_number() {
  Number number = {0};
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


// Step into the next block
i64 enter_block() {
  while (!is_token("{") && read_position < file_size) {
    read_position += 1;
  }
  read_position += 1;
  return success;
}

// Step out of the current block skipping inner blocks
i64 skip_block() {
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

// Skip until the next line
i64 skip_line() {
  while (!is_token("\n") && read_position < file_size) {
    read_position += 1;
  }
  read_position += 1;
  return success;
}

void skip_spaces() {
  while (is_token(" ") && read_position < file_size) {
    read_position += 1;
  }
}

// Prune the variables array by removing all variables at the current block level and then decrease the block level
void decrese_block_level() {
  i32 index = array_last(variables);
  bool same_level = true;
  while (index >= 0 && same_level) {
    Variable *variable = (Variable *)array_get(variables, index);
    if (variable->level == block_level) {
      array_pop(variables);
    } else {
      same_level = false;
    }
    index -= 1;
  }
  block_level -= 1;
}

// Get the index of a variable by name
i32 get_variable_index(char *name) {
  i32 index = array_length(variables) - 1;
  while (index >= 0) {
    Variable *variable = (Variable *)array_get(variables, index);
    if (strcmp(variable->name, name) == 0) {
      return index;
    }
    index -= 1;
  }
  return -1;
}

// Parses out a variable name and saves it either in the arena or on the heap
char *parse_name(bool save){
  skip_spaces();
  u8 *name_start = &file_data[read_position];
  i32 name_length = 0;
  while ((file_data[read_position] >= 'a' && file_data[read_position] <= 'z') || file_data[read_position] == '_') {
    name_length += 1;
    read_position += 1;
  }
  // Allocate memory for the variable name and copy it
  char *variable_name = 0;
  if (save) {
    variable_name = arena_fill(arena, sizeof(char) * (name_length + 1));
  } else {
    variable_name = malloc(sizeof(char) * (name_length + 1));
  }
  if (variable_name == NULL) {
    printf("Memory allocation failed in parse_get_variable\n");
    exit(1);
  }
  memcpy(variable_name, name_start, name_length);
  variable_name[name_length] = '\0'; // Null terminate the string
  return variable_name;
}

// Parses out a variable name and returns that item from the variables array
Number parse_get_variable() {
  char *variable_name = parse_name(false);
  i32 variable_index = get_variable_index(variable_name);
  Number variable_value = {0};
  if (variable_index >= 0) {
    Variable *variable = (Variable *)array_get(variables, variable_index);
    variable_value = variable->value;
  } else {
    printf("Error: Variable %s not found\n", variable_name);
    exit(1);
  }
  free(variable_name);
  return variable_value;
}

// Get the index of a snippet by name
i64 get_snippet_index(char *name) {
  i32 index = array_length(snippets) - 1;
  while (index >= 0) {
    Snippet *snippet = (Snippet *)array_get(snippets, index);
    if (strcmp(snippet->name, name) == 0) {
      return snippet->index;
    }
    index -= 1;
  }
  return -1;
}

// Parses out a snippet name and sets that item's index from the snippets array
void parse_get_snippet() {
  char *snippet_name = parse_name(false);
  i64 snippet_index = get_snippet_index(snippet_name);
  if (snippet_index == -1) {
    printf("Error: Snippet %s not found\n", snippet_name);
    exit(1);
  }
  // Put end of the line on the jump stack so the parser knows where to continue after the snippet
  skip_line();
  Jump return_jump = {
    .type = jumps.return_to,
    .index = read_position
  };
  array_push(jump_stack, &return_jump);
  // Set the read position to the snippet start
  read_position = snippet_index;
  block_level += 1;
  free(snippet_name);
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

// Compacts a number by removing trailing zeros
Number compact_number(Number number) {
  Number result = number;
  while (result.value % 10 == 0 && result.value != 0) {
    result.value /= 10;
    result.exponent += 1;
  }
  return result;
}

// Evaluate an expression
// Parser reads up to 3 numbers and 2 operators at a time and evaluates the operator with the highest priority first. This makes it possible to evaluate expressions like a + b * c * d + e accurately.
Number evaluate_expression() {
  Number result = {.value = 0, .exponent = 0};
  u8 parsed_numbers = 0; // Keep track of current parser window
  Number first_number = {.value = 0, .exponent = 0};
  Number second_number = {.value = 0, .exponent = 0};
  Number third_number = {.value = 0, .exponent = 0}; // For the case of a + b * c, we want to wait with the addition until we know the result of the multiplication
  u8 op_code = 0; // 1 = plus, 2 = minus, 3 = multiply, 4 = divide
  u8 op_code2 = 0;
  while (!is_token(")") && !is_token(";") && !is_token("<") && !is_token(">") && !is_token("=") && read_position < file_size) {
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
      // printf("Unknown token in eval\n");
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
  result = compact_number(result);
  return result;
}

// Parses out a variable name and a value and adds them as a new new item to the variables array
i64 new_variable() {
  char *variable_name = parse_name(true);
  // Skip spaces and =
  while (is_token(" ") || is_token("=")) {
    read_position += 1;
  }
  // Get the value
  Number value = evaluate_expression();
  read_position += 1; // Skip the trailing ;

  // Push the variable to the variables array
  Variable new_variable = {
    .value = value,
    .name = variable_name,
    .level = block_level
  };
  array_push(variables, &new_variable);
  return success;
}

// Parses out a snippet name and adds it to the snippets array
i64 new_snippet() {
  char *snippet_name = parse_name(true);
  enter_block();
  // Push the snippet to the snippets array
  Snippet new_snippet = {
    .name = snippet_name,
    .index = read_position
  };
  array_push(snippets, &new_snippet);
  // Snippet should not be evaluated before it's inserted
  skip_block();
  return success;
}

// Parses out a variable name, finds it in the variables array and updates it with a new value
i64 set_variable() {
  char *variable_name = parse_name(false);
  i32 variable_index = get_variable_index(variable_name);
  if (variable_index >= 0) {
    Variable *variable_ref = (Variable *)array_get(variables, variable_index);

    // Skip spaces and =
    while (is_token(" ") || is_token("=")) {
      read_position += 1;
    }
    // Get the value
    Number new_value = evaluate_expression();
    read_position += 1; // Skip the trailing ;
    variable_ref->value = new_value;
  } else {
    printf("Error: Variable %s not found\n", variable_name);
    exit(1);
  }
  free(variable_name);
  return success;
}


// Skips following else blocks after an if or else if block
void skip_elses() {
  while (is_token("else") && read_position < file_size) {
    read_position += 4;
    enter_block();
    skip_block();
    skip_spaces();
  }
}

bool evaluate_condition() {
  Number first_number = evaluate_expression();
  skip_spaces();
  // Get the operator
  u8 comparator = 0;
  if (is_token("==")) {
    comparator = comparators.equal_to;
    read_position += 2;
  } else if (is_token("<=")) {
    comparator = comparators.less_than_or_equal_to;
    read_position += 2;
  } else if (is_token("<")) {
    comparator = comparators.less_than;
    read_position += 1;
  } else if (is_token(">=")) {
    comparator = comparators.greater_than_or_equal_to;
    read_position += 2;
  } else if (is_token(">")) {
    comparator = comparators.greater_than;
    read_position += 1;
  }
  skip_spaces();
  Number second_number = evaluate_expression();
  align_exponents(&first_number, &second_number);

  // Compare the numbers
  bool result = false;
  if (comparator == comparators.equal_to) {
    result = first_number.value == second_number.value;
  } else if (comparator == comparators.less_than) {
    result = first_number.value < second_number.value;
  } else if (comparator == comparators.greater_than) {
    result = first_number.value > second_number.value;
  } else if (comparator == comparators.less_than_or_equal_to) {
    result = first_number.value <= second_number.value;
  } else if (comparator == comparators.greater_than_or_equal_to) {
    result = first_number.value >= second_number.value;
  }
  return result;
}

// Parser function
i32 parse_token() {
  // Skip spaces
  while (is_token(" ") || is_token("\n")) {
    read_position += 1;
  }
  if (is_token("}")) {
    read_position += 1;
    Jump *jump = (Jump *)array_pop(jump_stack);
    if (jump != 0) {
      decrese_block_level();
      if (jump->type == jumps.skip_else) {
        skip_spaces();
        skip_elses();
      } else if (jump->type == jumps.return_to) {
        read_position = jump->index;
      }
    }
  } else if (is_token("while")) {
    Jump iteration_jump = {
      .type = jumps.return_to,
      .index = read_position
    };
    read_position += 5;
    skip_spaces();
    read_position += 1; // skip start parenthesis
    if (evaluate_condition()) {
      array_push(jump_stack, &iteration_jump);
      enter_block();
      block_level += 1;
    } else {
      enter_block();
      skip_block();
    }
  } else if (is_token("if")) {
    read_position += 2;
    skip_spaces();
    read_position += 1; // skip start parenthesis
    if (evaluate_condition()) {
      array_push(jump_stack, &skip_else_jump);
      enter_block();
      block_level += 1;
    } else {
      enter_block();
      skip_block();
    }
  } else if (is_token("else if")) {
    read_position += 7;
    skip_spaces();
    read_position += 1; // skip start parenthesis
    if (evaluate_condition()) {
      array_push(jump_stack, &skip_else_jump);
      enter_block();
      block_level += 1;
    } else {
      enter_block();
      skip_block();
    }
  } else if (is_token("else")) {
    read_position += 4;
    enter_block();
    block_level += 1;
  }
  // New variable
  else if (is_token("num")) {
    read_position += 3;
    new_variable();
  }
  // Insert snippet
  else if(is_token("use")) {
    read_position += 3;
    parse_get_snippet();
  }
  // New snippet
  else if (is_token("def")) {
    read_position += 3;
    new_snippet();
  }
  // Comment
  else if (is_token("//")) {
    skip_line();
  }
  // Print statement
  else if (is_token("print")) {
    read_position += 6;
    Number result = evaluate_expression();
    printf("%lld * 10^%d\n", result.value, result.exponent);
    skip_line();
  }
  // Existing variable
  else if (file_data[read_position] >= 'a' && file_data[read_position] <= 'z') {
    set_variable();
  }
  // Other token
  else {
    printf("Unknown token: %c\n", file_data[read_position]);
    read_position += 1;
  }
  return success;
}

i32 main(i32 arg_count, char *arguments[]) {
  if (arg_count != 2) {
    printf("Tarzan wants: %s <filename>\n", arguments[0]);
    return 1;
  }

  FILE *file = fopen(arguments[1], "r");
  if (file == NULL) {
    printf("Tarzan can't open file %s\n", arguments[1]);
    return 1;
  }

  i32 time_start = clock();
  // Copy the file into memory
  fseek(file, 0, SEEK_END);
  file_size = ftell(file);
  fseek(file, 0, SEEK_SET);
  arena = arena_open(file_size);
  file_data = (u8 *)arena_fill(arena, file_size);
  fread(file_data, 1, file_size, file);

  // Initialize variables and jump stack arrays
  variables = array_create(arena, sizeof(Variable));
  snippets = array_create(arena, sizeof(Snippet));
  jump_stack = array_create(arena, sizeof(Jump));

  // Parse the file
  while (read_position < file_size) {
    parse_token();
  }
  arena_close(arena);
  fclose(file);
  i32 time_end = clock();
  printf("Tarzan done in %dms!\n", (time_end - time_start) / (CLOCKS_PER_SEC / 1000));
  return 0;
}