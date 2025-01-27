# tarzan-lang
 
Tarzan is a tiny interpreted language with C-like syntax. The interpreter is a simple line-by-line interpreter written in C9.

Compiling:
```
clang -std=c99 -Wall -Wextra -O2 tarzan.c -o tarzan
```

Running:
```
./tarzan <filename>
```