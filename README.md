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
// min language spec (v1.00 wip - 2022.10)
// - public domain.

/////////////////////////////////////////// operators
& | ~ ^ ! << >>                          // bitwise
< <= == != => > && ||                    // conditional, logical
+ - * ** / % ++ -- @ [] . ->             // arithmetical, indexing, member accessing (** for pow, [] or @ for indexing)
= += -= *= **= /= %= &= |= ~= ^= <<= >>= // assignments

//////////////////////////// types
'!' '\x1' '\u1234'        // 32-bit char (ascii, hexadecimal and utf notations)
16  0x10                  // 53-bit integer (decimal and hexadecimal notations)
16. 16.0  16.f            // 64-bit float (different notations)
"16" '16'                 // string: both pairing quotes allowed
true false                // bools: any mismatching condition, zero value or empty string/list/map eval to `false`; `true` otherwise

//////////////////////////// statements
statement();              // semi-colons at end of line are optional
multiple(); statements()  // semi-colons between statements within same line are mandatory
{ more(); statements() }  // multi-line scopes use {braced scopes}
: more(); statements()    // single-line scopes from colon `:` till end of line

//////////////////////////// conditions
if(2+2 >= 4): put("Four") // parentheses in expressions are optional
elif "a"<"b": put("Sort") // unconstrained `else if` could also be used
else {put("Last resort")} // notice that classic {braced scopes} can also be used
                          //
sign = PI > 0 ? +1 : -1   // ternary operator `expr ? ... : ...`

//////////////////////////// loops
s = "Hello"               //
while len(s) < 25: s+="!" // `while(expr) ...`
                          //
do { s += "Hi!" }         // `do ... while(expr)`
while len(s) < 50         // loops may use `break`/`continue`/`return` to alter flow

//////////////////////////// functions (hint: `self` or `self_[anything]` named args are mutables)
triple(n=1): n*3          // optional arg `n`. optional `return` keyword; last stack value implicitly returned
triple()                  // 3
triple(5)                 // 15
triple(n=10)              // 30. named argument
copy = triple             // capture function
copy(10)                  // also 30

//////////////////////////// unified call syntax
len("hi"); "hi".len()     // these two calls are equivalent...
add(s1,s2); s1.add(s2)    // ...so these two as well
s1.add(s2).add(s3)        // UCS allows chaining

//////////////////////////// strings
hi='hello'                // strings can use "quotes" or 'quotes' at your discretion
string=f"{hi} world"      // "hello world". f-interpolated string
string[1]                 // 'e'. positive indexing from first element [0]
string[-3]                // 'r'. negative indexing from last element [-1]
string < string           // comparison (<,>,==,!=)
string + string           // "hello worldhello world". concatenation
string - string           // "". removes all occurences of right string in left string
.hash() .len() .less() .index() .put() .quote() .assert()
.pop() .back() .del([:]) .find(ch) .rfind(ch) .starts(s) .ends(s) .split(s) .slice(i,j)

//////////////////////////// lists (either vector or linked-list at vendor discretion)
list = [2, 4, 6, 8]       //
list[0]                   // 2. positive indexing from first element [0]
list[-2]=5                // [2,4,5,8]. negative indexing from last element [-1]
.len() .del([:]) .find(lst) .starts(lst) .ends(lst) .join(sep) .slice(i,j[,k]) .reverse() .shuffle() .index(i) .put()

///////////////////////////////// EXTENSIONS BELOW /////////////////////////////////////////////

//////////////////////////// EXTENSION: maps (either ordered or unordered at vendor discretion)
map = {"a":1,'b':2,c:3}   // keys can be specified with different styles
map.a                     // 1
map->b                    // 2
map["c"]                  // 3
.len() .del([:]) .find(x) .keys(wc) .values(wc) .sort(op) .slice(i,j) .index(k) .put()

///////////////////////////////////////////////// EXTENSION: classes
// classes are maps with a special `isa` entry //
// that points to the parent class and is set  //
// automatically at instance time.             //
/////////////////////////////////////////////////
Shape = { sides: 0 }                           // plain data struct. methods below:
Shape(Shape self, n=1): self.sides=n           // constructor
~Shape(Shape self):                            // destructor
degrees(Shape self): self.sides*180-360        // method
// instance base class //////////////////////////
Square = Shape(4)                              //
Square.isa                                     // "Shape"
Square.sides                                   // 4
// instance derived class ///////////////////////
x = Square()                                   //
x.isa                                          // "Square"
x.degrees()                                    // 360

/////////////////////////////////// EXTENSION: extended if/while conditionals
if a = 10; a < b: put(a)         // `init[...]; if cond:` alias
while a = 10; a < b: put(a)      // `init[...]; while cond:` alias

/////////////////////////////////// EXTENSION: for
for a = 10; a < b; ++a: put(a)   // `init[...]; while cond: {code;post}` alias
for i in 10..1: put(i + "...")   //

/////////////////////////////////// EXTENSION: casts
int(val)                         // cast value to int. only if specialization exists
bool(val)                        // cast value to bool. only if specialization exists
char(val)                        // cast value to char. only if specialization exists
float(val)                       // cast value to float. only if specialization exists
string(val)                      // cast value to string. only if specialization exists
vec2(vec3 a) { vec2(a.x,a.y) }   // define a custom vec3->vec2 specialization
vec2(v3)                         // vec3 to vec2 casting is allowed now

/////////////////////////////////// EXTENSION: match
match var {                      //
    1: put('one')                //
    1..99: put('number')         //
    'a'..'z': put('letter')      //
}                                //

/////////////////////////////////// EXTENSION: exceptions
throw("my exception message")    // `throw(value)`, `thrown` keywords
msg = try(fn(x)) ? "ok" : thrown // `try(expression)` returns `true` if no exceptions were thrown; `false` otherwise

/////////////////////////////// EXTENSION: set theory
list+list or map+map         // union [1,2]+[2,3] -> [1,2,3]
list-list or map-map         // difference [1,2]-[2,3] -> [1]
list^list or map^map         // intersection [1,2]^[2,3] -> [2]

/////////////////////////////// EXTENSION: slices [i:j] `i,j` are optional
map["b":"a"]                 // {b:2,a:1}. slices [i:j] from i up to j (included). defaults to [first_key:last_key]
list[-1:1]                   // [8,5,4].   slices [i:j] from i up to j (included). defaults to [0:-1]
string[7:9]                  // "wo".      slices [i:j] from i up to j (included). defaults to [0:-1]
string[7:]                   // "world"
string[-1:0]                 // "dlrow olleh"

/////////////////////////////// EXTENSION: ranges [i..j[..k]]
3..0                         // [3,2,1,0]. returns list of integers from 3 up to 0 (included)
7..0..3                      // [7,4,1,0]. returns list of integers from 7 up to 0 (included) 3 steps each
'A'..'D'                     // [A,B,C,D]. returns list of chars from A up to D (included)

//////////////////////////// EXTENSION: in
for i in -1..1: put(i)    // returns iterator that walks lists,maps and objects -> -1 0 1
for k,v in vec2: put(k,v) // iterate key/value pairs of given object type -> "isa""vec2" "x"0 "y"0
                          // `while i in 10..1` is also valid

////////////////////////// EXTENSION: lambdas
mul = 5                 //
calc = fn(n): n*mul     // `fn` keyword denotes a following lambda function
calc(10)                // 50

/////////////////////// EXTENSION: eval
eval("1+2")          // 3
put(f"{1+2}==3")     // "3==3". any {moustache} in a f-string redirects to an internal `eval` operator call

///////////////////// EXTENSION: templates
fn min(T a, T b):  // single-capital-letters will expand to different types when used
    a < b ? a : b  //

/////////////// EXTENSION: dlsym
sin(3.14159) // foreign calls are implicitly passed to underlying runtime via `dlsym()` call

////////////// EXTENSION: typeof
typeof(3.0) // "float"
typeof(v2)  // "vec2"

////////////////////////////////////////////
// preprocessor and pragmas: @todoc
// ffi and C abi convention: @todoc
// constness, symbol visibility: @todoc
// embedding, calling from/to C: @todoc func = dlsym("int puts(const char *)@my_dll"); put(func)
// std modules: matching math.h, string.h, stdio.h @todoc
// match: see rust
// keywords: true/false if/elif/else for/in/do/while break/continue/return fn isa @todoc
// modules: put("once") ; return fn(x) { ... } --- x = import('file.src', 'init args'...)
```
