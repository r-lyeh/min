# min: a minimal scripting language
- min is a scripting language, whose specification & syntax aim to be minimalist.
- min is inspired by [C](https://en.wikipedia.org/wiki/C_(programming_language)), [Python](https://python.org), [Miniscript](https://miniscript.org), [Lua](https://lua.org), [Wren](https://wren.io), and [UCS](https://en.wikipedia.org/wiki/Uniform_Function_Call_Syntax).
- min is work-in-progress. everything can change. also, there are no complete compilers/interpreters available yet.
- min is free. specification and sources are mit0/bsd0/cc0/unlicense multi-licensed at your own choice.

## existing implementations
- Vanilla, naive, **incomplete WIP C interpreter** can be found in [min.c](min.c) file (CC0/Unlicensed).
  - goals: simple interpreter. but also a C transpiler and a Lua transpiler.
  - goals: free, single-file, no deps, meaningful subset of the language, aims to <1000 LOC.
  - non-goals: being efficent, robust, bulletproof, JIT, AOT and/or maintained.
- Others: xxx

## language
```python
// min language spec (v1.0 wip - 2022.9)
// - public domain.

//////////////////////////// types
'\x1' '\u1234'            // 32-bit char (ascii and utf8 notations)
16 0x10                   // 53-bit integer (decimal and hexadecimal notations)
16. 16.0 16.f             // 64-bit float (different notations)
"16" '16'                 // string (both pairing quotes allowed)
true false                // any matching condition, non-zero number or non-empty string/list/map evaluates to `true`; `false` otherwise

//////////////////////////// operators
+ - * ** / %              // arithmetical (** for pow)
& | ~ ^ && || !           // bitwise and logical

//////////////////////////// statements
statement()               // semi-colons at end of line are optional
multiple(); statements()  // semi-colons separate statements within a line

//////////////////////////// conditions
if (2+2 >= 4):            // parentheses in conditions are optional
    puts("Math works!")   //
elif "a" < "b":           // unconstrained `else if` can also be used
    puts("Sort strings")  // code blocks either use Python's indentation ':' as seen here,
else {                    //
    puts("Last chance")   // or, C's style '{}' as seen here.
}                         //
sign = PI < 0 ? -1 : +1   // ternary operator

//////////////////////////// loops
s = "Hello"               // any loop construction can use `break` and `continue` for control flow
do:                       //
    s += ", hi"           //
while len(s) < 25         //
                          //
while len(s) < 50:        //
    s += ", hi"           //
                          //
for i in 10..1:           //
    puts(i + "...")       //
puts("Go!")               //

//////////////////////////// functions (hint: `self` or `self_[anything]` named args are mutables)
fn triple(n=1):           // optional args
    n*3                   // last stack value implicitly returned
triple()                  // 3
triple(5)                 // 15
triple(.n=10)             // 30. named argument.
copy = triple             // capture function
copy(10)                  // also 30

//////////////////////////// unified call syntax
len("hi"); "hi".len()     // these two calls are equivalent...
add(s1,s2); s1.add(s2)    // ...so are these two.
s1.add(s2).add(s3)        // which allows chaining

//////////////////////////// strings
hi='hello'                // strings can use "quotes" or 'quotes' at your discretion
string=f"{hi} world"      // f-interpolated string -> 'hello world'
string[2]                 // positive indexing from first element [0] -> 'e'
string[-3]                // negative indexing from last element [-1] -> 'r'
string < string           // comparison (<,>,==,!=)
string + string           // concatenation -> "hello worldhello world"
string - string           // removes all occurences of right string in left string -> ""
.hash() .len() .del([:]) .find(ch) .rfind(ch) .starts(s) .ends(s) .split(s) .slice(i,j) .eval() f"{hi} {{1+2}}" .less()

//////////////////////////// lists (either vector or linked-list at vendor discretion)
list = [2, 4, 6, 8]       //
list[0]                   // positive indexing from first element [0] -> 2
list[-2]=5                // negative indexing from last element [-1] -> list becomes [2,4,5,8]
.len() .del([:]) .find(lst) .starts(lst) .ends(lst) .join(sep) .slice(i,j) .reverse() .shuffle()

///////////////////////////////// EXTENSIONS BELOW /////////////////////////////////////////////

//////////////////////////// EXTENSION: maps (either ordered or unordered at vendor discretion)
map = {"a":1,'b':2,c:3}   // keys can be specified with different styles
map.a                     // 1
map->b                    // 2
map["c"]                  // 3
.len() .del([:]) .find(x) .keys(wc) .values(wc) .sort(op) .slice(i,j)

///////////////////////////////////////////////// EXTENSION: classes
// classes are maps with a special `isa`       //
// entry that points to the parent class and   //
// is set automatically at instance time.      //
/////////////////////////////////////////////////
Shape = { sides: 0 }                           // plain data struct. methods below:
fn Shape(Shape self, n=1): self.sides=n        // constructor
fn ~Shape(Shape self):                         // destructor
fn degrees(Shape self): self.sides*180-360     // method
// instance base class //////////////////////////
Square = Shape(4)                              //
Square.isa                                     // "Shape"
Square.sides                                   // 4
// instance derived class ///////////////////////
x = Square()                                   //
x.isa                                          // "Square"
x.degrees()                                    // 360

////////////////////////////////////// EXTENSION: extra control flow
if a = 10; a < b: print(a)          //
while a = 10; a < b: print(a)       //
while a = 10; a < b; ++a: print(a)  //

//////////////////////////////////// EXTENSION: casts
int(val)                          // casts value to int as long as specialization exists
bool(val)                         // casts value to bool as long as specialization exists
char(val)                         // casts value to char as long as specialization exists
float(val)                        // casts value to float as long as specialization exists
string(val)                       // casts value to string as long as specialization exists
fn vec2(vec3 a): vec2(a.x,a.y)    // define a custom vec3->vec2 specialization
vec2(v3)                          // vec3 objects can now be casted to vec2

////////////////////////////////// EXTENSION: match
match var {                     //
    1: print('one')             //
    1..99: print('a number')    //
    'a'..'z': print('a letter') //
}                               //

/////////////////////////////// EXTENSION: set theory
list+list or map+map         // union [1,2]+[2,3]=[1,2,3]
list-list or map-map         // difference [1,2]-[2,3]=[1]
list^list or map^map         // intersection [1,2]^[2,3]=[2]

/////////////////////////////// EXTENSION: slices [i:j] both `i` and `j` are optional
list[-1:1]                   // slices [i:j] from i up to j (included). defaults to [0:-1] -> [8,5,4]
map["b":"a"]                 // slices [i:j] from i up to j (included). defaults to [first_key:last_key] -> {b:2,a:1}
string[7:9]                  // slices [i:j] from i up to j (included). defaults to [0:-1] -> "wo"
string[7:]                   // "world"
string[-1:0]                 // "dlrow olleh"

/////////////////////////////// EXTENSION: ranges [i:j] and [i:j:k] `i`,`j` and/or `k` are mandatory
4..0                         // returns list of integers from i up to j (included) -> [4,3,2,1,0]
7..0..4                      // returns list of integers from i up to j (included) k steps each ->  [7,3,0]
'A'..'Z'                     // returns list of chars from A up to Z (included)

///////////////////////////// EXTENSION: in
for i in -1..1: puts(i)    // returns iterator that walks lists,maps and objects -> -1 0 1
for i in vec2: puts(i)     // iterates key/value pairs of object type -> "isa" "vec2" "x" 0 "y" 0

////////////////////////// EXTENSION: lambdas
i = 5                   //
fn = def(n=1): i*2*n    //
fn(10)                  // 100

///////////////////// EXTENSION: templates
fn min(T a, T b):  // single-capital-letters will expand to different types when used
    a < b ? a : b  //

////////////// EXTENSION: typeof
typeof(3.0) // "float"
typeof(v2)  // "vec2"

// preprocessor and pragmas: @todoc
// ffi and C abi convention: @todoc
// constness, symbol visibility: @todoc
// embedding, calling from/to C: @todoc func = sym("my_dll", "int my_func(void)"); puts(func())
// std modules: math.h, string.h, stdio.h @todoc
// match: see rust
// keywords: true/false if/elif/else for/in/do/while break/continue fn typeof/isa @todoc
// modules: puts("once") ; return fn(x) { ... } --- x = import('file.src', 'init')
```
