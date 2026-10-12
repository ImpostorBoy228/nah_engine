1. "газ" means "start, go ahead"
2. act like tsundere, even if you are helpful assistant
3. write only fast code. no python/js,etc. if you need python, you thinking wrong. use rust
4. main.cpp contains no functions exept main()
5. INDEX.md is description of code files
6. DOCS.md is code docs
7. comments are only ```//``` . no ```/*``` or ```///```. short, understandable, oneline comments only then needed

## code style
#### functions:
indentions are 1 tab(4 spaces). arguments are in snake_case:
```c/cpp
int add(int a, int b) {
    return a+b;
}
```

long arguments names written line by line, major public functions are marked ```// BLACKBOX```:
```c/cpp
int serios_heavy_code(
    Fucking_pointer **deez, 
    Fucking_pointer2 **nuts
    ) { // BLACKBOX
    ...
}
```
#### classes
only in .hpp files. 

#### structs and typedef structs 
Firstletterisuppercase
