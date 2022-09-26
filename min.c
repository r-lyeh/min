// min.c scripting language: reference implementation (WIP! WIP! WIP!)
// - rlyeh, public domain.

int VERBOSE = 0; // if true, outputs debugging info

#line 1 "utils.h"
#include <assert.h>
#include <ctype.h>
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _MSC_VER
#define __thread         __declspec(thread)
#elif defined __TINYC__ && defined _WIN32
#define __thread         __declspec(thread) // compiles fine, but does not work apparently
#elif defined __TINYC__
#define __thread
#endif

// -----------------------------------------------------------------------------
// memory (vector based allocator; x1.75 enlarge factor)

void* vrealloc( void* p, size_t sz ) {
    if( !sz ) {
        if( p ) {
            size_t *ret = (size_t*)p - 2;
            ret[0] = 0;
            ret[1] = 0;
            (void)realloc( ret, 0 );
        }
        return 0;
    } else {
        size_t *ret;
        if( !p ) {
            ret = (size_t*)realloc( 0, sizeof(size_t) * 2 + sz );
            ret[0] = sz;
            ret[1] = 0;
        } else {
            ret = (size_t*)p - 2;
            size_t osz = ret[0]; // original size
            size_t cap = ret[1]; // capacity
            if( sz == (size_t)-1 ) return (void*)osz; // @addme
            if( sz <= (osz + cap) ) {
                ret[0] = sz;
                ret[1] = cap - (sz - osz);
            } else {
                ret = (size_t*)realloc( ret, sizeof(size_t) * 2 + sz * 1.75 );
                ret[0] = sz;
                ret[1] = (size_t)(sz * 1.75) - sz;
            }
        }
        return &ret[2];
    }
}
size_t vlen( void* p ) {
    return (size_t)vrealloc( p, (size_t)-1 ); // @addme
}

// -----------------------------------------------------------------------------
// arrays

#ifdef __cplusplus
#define array_cast(x) (decltype x)
#else
#define array_cast(x) (void *)
#endif

#define array(t) t*
#define array_resize(t, n) ( array_c_ = array_count(t), array_n_ = (n), array_realloc_((t),array_n_), (array_n_>array_c_? memset(array_c_+(t),0,(array_n_-array_c_)*sizeof(0[t])) : (void*)0), (t) )
#define array_push(t, ...) ( array_realloc_((t),array_count(t)+1), (t)[ array_count(t) - 1 ] = (__VA_ARGS__) )
#define array_pop(t) ( array_c_ = array_count(t), array_c_ ? array_realloc_((t), array_c_-1) : array_cast(t)(NULL) ) // @addme
#define array_back(t) ( &(t)[ array_count(t)-1 ] )
#define array_count(t) (int)( (t) ? ( vlen(t) - sizeof(0[t]) ) / sizeof(0[t]) : 0u )
#define array_clear(t) ( array_realloc_((t),0) ) // -1
#define array_free(t) ( array_realloc_((t),-1), (t) = 0 ) // -1
#define array_realloc_(t,n)  ( (t) = array_cast(t) vrealloc((t), ((n)+1) * sizeof(0[t])) ) // +1

static __thread unsigned array_c_, array_n_;

// -----------------------------------------------------------------------------
// strings

#define  va(...)             ((printf || printf(__VA_ARGS__), va(__VA_ARGS__)))  // vs2015 check trick
#define  strcatf(s,fmt,...)  strcatf((s), va(fmt, __VA_ARGS__))

char* (va)(const char *fmt, ...) {
    va_list vl;
    va_start(vl, fmt);

        va_list copy;
        va_copy(copy, vl);
        int sz = /*stbsp_*/vsnprintf( 0, 0, fmt, copy ) + 1;
        va_end(copy);

        int reqlen = sz;
        enum { STACK_ALLOC = 128*1024 };
        static __thread char *buf = 0; if(!buf) buf = vrealloc(0, STACK_ALLOC); // @leak
        static __thread int cur = 0, len = STACK_ALLOC - 1; //printf("string stack %d/%d\n", cur, STACK_ALLOC);

        assert(reqlen < STACK_ALLOC && "no stack enough, increase STACK_ALLOC variable above");
        char* ptr = buf + (cur *= (cur+reqlen) < len, (cur += reqlen) - reqlen);

        /*stbsp_*/vsnprintf( ptr, sz, fmt, vl );

    va_end(vl);
    return (char *)ptr;
}
char* (strcatf)(char **src_, const char *buf) {
    char *src = *src_;
        if(!buf) return src;
        if(!buf[0]) return src;
        int srclen = (src ? strlen(src) : 0), buflen = strlen(buf);
        src = (char*)vrealloc(src, srclen + buflen + 1 );
        memcpy(src + srclen, buf, buflen + 1 );
    *src_ = src;
    return src;
}
array(char*) split(const char *str, const char *separators, int include_separators) {
    static __thread int slot = 0;
    static __thread char *buf[16] = {0};
    static __thread array(char*) list[16] = {0};

    slot = (slot+1) % 16;
    array_resize(list[slot], 0);
    *(buf[slot] = vrealloc(buf[slot], strlen(str)*2+1)) = '\0'; // *2 to backup pathological case where input str is only separators && include == 1

    for(char *dst = buf[slot]; str && *str; ) {
        // count literal run && terminators
        int run = strcspn(str, separators);
        int end = strspn(str + run, separators);

        // append literal run
        if( run ) {
            array_push(list[slot], dst);
            memmove(dst,str,run); dst[run] = '\0'; //strncpy(dst, str, run)
            dst += run + 1;
        }
        // mode: append all separators: "1++2" -> "1" "+" "+" "2"
        if( include_separators )
        for( int i = 0; i < end; ++i ) {
            array_push(list[slot], dst);
            dst[0] = str[ run + i ];
            dst[1] = '\0';
            dst += 2;
        }

        // skip both
        str += run + end;
    }

    return list[slot];
}

// -----------------------------------------------------------------------------
// rpn (Shunting-Yard)
#line 1 "rpn.c"

const char *operators = // from https://en.cppreference.com/w/c/language/operator_precedence
    "\x01`++`\x01`--`"                      // ++ --           Suffix/postfix increment and decrement  Left-to-right
//  "\x01`(`\x01`)`"                        // ()              Function call
"\x01`@`"       "\x01`[`\x01`]`"            // []              Array subscripting
    "\x01`.`\x01`->`"                       // . ->            Structure and union member accesses
//  "\x01``"                                // (type){list}    Compound literal(C99)
    "\x02`++`\x02`--`"                      // ++ --           Prefix increment and decrement  Right-to-left
//  "\x02`+`\x02`-`"                        // + -             Unary plus and minus
    "\x02`!`\x02`~`"                        // ! ~             Logical NOT and bitwise NOT
//  "\x02`(type)`"                          // (type)          Cast
//  "\x02`*`\x02`"                          // *               Indirection (dereference)
//  "\x02`&`\x02`"                          // &               Address-of
//  "\x02`sizeof`"                          // sizeof          Size-of[note 2]
//  "\x02`_Alignof`"                        // _Alignof        Alignment requirement(C11)
    "\x02`**`"                              // **              Pow
    "\x03`*`\x03`/`\x03`%`"                 // * / %           Multiplication, division, and remainder Left-to-right
    "\x04`+`\x04`-`"                        // + -             Addition and subtraction
    "\x05`<<`\x05`>>`"                      // << >>           Bitwise left shift and right shift
    "\x06`<`\x06`<=`\x06`>`\x06`>=`"        // < <=            For relational operators
    "\x07`==`\x07`!=`"                      // == !=           For relational respectively
    "\x08`&`"                               // &               Bitwise AND
    "\x09`^`"                               // ^               Bitwise XOR (exclusive or)
    "\x10`|`"                               // |               Bitwise OR (inclusive or)
    "\x11`&&`"                              // &&              Logical AND
    "\x12`||`"                              // ||              Logical OR
//  "\x13`?:`"                              // ?:              Ternary conditional[note 3] Right-to-left
    "\x14`=`"                               // =               Simple assignment
    "\x14`+=`\x14`-=`"                      // += -=           Assignment by sum and difference
    "\x14`*=`\x14`/=`\x14`%=`"              // *= /= %=        Assignment by product, quotient, and remainder
//  "\x14`<<=`\x14`>>=`"                    // <<= >>=         Assignment by bitwise left shift and right shift
    "\x14`&=`\x14`^=`\x14`|=`"              // &= ^= |=        Assignment by bitwise AND, XOR, and OR
    "\x15`,`"                               // ,               Comma   Left-to-right
;

array(char*) rpn(array(char*) tokens) { // [01] [+-] [01] -> [0]1 [0]1 [+]-
    array(char*) out = 0;
    array(char*) s = 0; // stack of indexes to operators

    for( int i = 0; i < array_count(tokens); ++i ) {
        char *c = tokens[i] + 1;

        // check if is_operator
        char *key = va("`%s`", c);
        char *found = strstr(operators, va("`%s`", c));

if(*c == '.' || !strcmp(c, "->")) ; // continue; // c = "+."; // field
if(c[-1] == 'f') { array_push(s, c); continue; } // functions

        /****/ if (*c == '(' ) {
            array_push(s, 1+"(("); // hardcoded ( operator
        } else if (*c == ')' ) {
            // until '(' on stack, pop operators.
            while (**array_back(s) != '(') {
                const char *token = *array_back(s);
                array_push(out, 1+va("%c%s",token[-1],token)); // 1+va("+%.*s", (int)(strchr(token,'`')-token), token));
                array_pop(s);
            }
            array_pop(s);
        } else if( !found ) {
            array_push(out, c);
        } else {
            ++found;
            while ( array_count(s) && **array_back(s) != '(' ) {
                char* back = *array_back(s);
                int prec2 = found[-2];
                int prec1 = back[-1] == 'f' ? 0x01 : strstr(operators, va("`%s`", back))[-1]; // back/s are in %c%s format, not %d`%s`
                // printf("(%#x)%c (%#x)%c\n", prec1, found[0], prec2, back[0]);
                int left1 = prec1 != 0x02 && prec1 != 0x13 && prec1 != 0x14; // !strstr(right_ops, key);
                if ((100-prec1) > (100-prec2) || (prec2 == prec1 && left1)) {
                    const char *token = *array_back(s);
                    array_push(out, 1+va("%c%s",token[-1],token));
                    array_pop(s);
                } else break;
            }
            array_push(s, 1+va("+%.*s", (int)(strchr(found,'`')-found), found) );
        }
    }

    while (array_count(s)) {
        const char *token = *array_back(s);
        array_push(out, 1+va("%c%s",token[-1],token)); // (int)(strchr(token,'`')-token), token));
        array_pop(s);
    }

    return out;
}

// -----------------------------------------------------------------------------
#line 1 "parser.c"

typedef struct node {
    array(char*) rpn;

    struct node *parent;  // "int" > "main" > "(i)" > "{ while(i<10) ++i; }" <<<<<<<<<<<<<< siblings. no parents. {block} has children.
    struct node *sibling; //                           \> "while" > "(i < 10)" > "++i" <<<< siblings and children of {block}. (block) has children.
    struct node *child;   //                                         \> "i" > "<" > "10" << siblings and children of (block). no children.
    struct node *skip;    // shortcut to next statement
    struct node *prev;

    char type;
    char *debug;
    char *debug_rpn;
} node;

node root, *env = &root, *sta = &root, *parent; // env+sta+parent are temporaries used to populate env hierarchy (siblings+skip+parent)

int parse_string_extended; // if true, function below will decode <abc> strings in addition to regular 'abc' and "abc" strings
int parse_string(const char *begin, const char *end) {
    const char *s = begin;
    if( s < end ) {
        if( *begin == 'f' ) { // f-interpolated strings
            int ret = parse_string(begin+1, end);
            return ret ? ret + 1 : 0;
        }
        if( *begin == '\'' || *begin == '"' || (parse_string_extended ? *begin == '<' : 0) ) {
            const char *close = *begin != '\'' && *begin != '"' && parse_string_extended ? ">" : begin ;
            do {
                ++s;
                if( *s == '\\' ) {
                    if( s[1] == '\\' ) s += 2;
                    else if( s[1] == *close ) s += 2;
                }
            } while (s <= end && *s != *close);

            return s <= end && *s == *close ? (int)(s + 1 - begin) : 0;
        }
    }
    return 0;
}

int found;

int parse_token(const char *begin, const char *end) {
    found = 0;

    const char *s = begin;

#   define eos (s > end)

    if( !found ) { // string literals
        int run = parse_string(s, end);
        s += run;
        found = /*!eos &&*/ run ? '\"' : 0;
    }

    if(!found) while( !eos && (*s <= 32) ) { // whitespaces, tabs & linefeeds
        found = ' ';
        ++s;
    }

    if(!found) while( !eos && (*s == '#') ) { // preprocessor
        found = '#';
        ++s;
        while( !eos && (*s >= 32) ) {
            /**/ if( *s == '\\' && (s[1] == '\r' && s[2] == '\n') ) s += 3; // @fixme: should be considered a (b)lank and not (p)reprocessor
            else if( *s == '\\' && (s[1] == '\r' || s[1] == '\n') ) s += 2; // @fixme: should be considered a (b)lank and not (p)reprocessor
            else ++s;
        }
    }

    if(!found) while( !eos && (*s == '/' && s[1] == '/') ) { // C++ comments
        found = '/';
        ++s;
        while( !eos && (*s >= 32) ) {
            ++s;
        }
    }

    if(!found) while( !eos && (*s == '/' && s[1] == '*') ) { // C comments
        found = '/';
        s += 2;
        while( !eos && !(*s == '*' && s[1] == '/') ) ++s;
           if( !eos &&  (*s == '*' && s[1] == '/') ) s += 2;
    }

    if(!found) while( !eos && (isalpha(*s) || *s == '_') ) { // keywords
        found = '_';
        ++s;
        while( !eos && isdigit(*s) ) {
            ++s;
        }
    }

    if(!found) while( !eos && strchr(",", *s) ) { // separators
        found = ',';
        ++s;
    }

    if(!found) while( !eos && strchr(";", *s) ) { // terminators
        found = ';';
        ++s;
    }

    if(!found) if(!eos) {
        if( *s == '.' && (s[1] == '_' || isalpha(s[1])) ) {
            found = '.'; // oop method invokation: 4.print() "hello".len()
            ++s;
        }
    }

    if(!found) if(!eos) { // numbers. @fixme: write a more canonical float parser
        if( s[0] == '.' || isdigit(*s) ) {
            int is_range = 0;

            found = '0';
            ++s;

            const char *parser = ".0123456789ef"; // .3f 1e4
            const char *hexparser = ".0123456789ABCDEFabcdef";
            if( s[0] == 'x' ) ++s, parser = hexparser;

            while( !eos && strchr(parser, *s) ) {
                if( s[0] == '.' && s[1] == '.' ) found = 'R'; // not a number; a range instead! 3..10
                if( s[0] == 'e' && s[1] == '-' ) ++s; // allow 1e-4
                ++s;
            }

            if( s[-1] == '.' && (*s == '_' || isalpha(*s)) ) { // dot not part of a number; method invokation instead! 4.say()
                --s; // ''
            }
        }
    }

    if(!found) if( !eos && strchr("<=>!&|~^+-/*%.?:\\@", *s) ) { // operators
        found = '+';
        // handle multi-digit operators as well
        if(0); // if( s[1] == *s && s[2] == *s && strchr("*<>", *s)) s+=3; // @todo: **= <<= >>=
        else if( s[1] == *s  && strchr("&|+-*", *s) ) s+=2; // @todo: add << >>
        else if( s[1] == '=' && strchr("<=>!&|~^+-/*%", *s) ) s+=2;
        else ++s;
    }

    if(!found) if( !eos ) { // () [] {} sub-blocks
        int depth = 0, endchar = *s == '(' ? ')' : *s == '[' ? ']' : *s == '{' ? '}' : '\0';
        if( endchar ) do {

            int run = parse_string(s, end); // bypass chars/strings with possible endchars on them
            s += run;
            if( eos ) break;

            depth += (*s == *begin) - (*s == endchar);
            found += depth > 0;
            ++s;
        } while( !eos && depth );
        if( found ) found = *begin;
    }

#   undef eos

    return found ? (int)(s - begin) : 0;
}

void flat(node *env, array(char*)*tokens ) {
    for( node *m = env; m && m->type; m = m->sibling ) {
        if( m->child && m->debug[0] == '[' && m->prev && (m->prev->type == '_' || m->prev->type == '\"' || m->prev->type == '[' || m->prev->type == '(') ) {
            array_push(*tokens, strdup("@@"));
            array_push(*tokens, strdup("(("));
            flat(m->child, tokens);
            array_push(*tokens, strdup("))"));
        }
        else
        if( m->child && (m->debug[0] == '(' || m->debug[0] == '[') ) {
            if( m->debug[0] == '[' ) array_push(*tokens, strdup("[["));
            array_push(*tokens, strdup("(("));
            flat(m->child, tokens);
            array_push(*tokens, strdup("))"));
        }
        else {
            if( !m->prev && (m->debug[0] == '+' || m->debug[0] == '-') && !m->debug[1] )
            array_push(*tokens, strdup("00")); // patch unary into binary

            array_push(*tokens, strdup(va("%c%s", m->type, m->debug)));
        }
    }
}

node* parse(const char *begin, const char *end) {
    node *env_begin = env;

    const char *s = begin;
    for( int read = parse_token(s, end); read; s += read, read = parse_token(s, end) ) {

        static int tabs = 0;
        if( found == ' ' || found == '/' ) continue; // skip (b)lanks and (c)omments from parsing
        if( VERBOSE ) printf(";; parser type(%c):len(%02d):%*.s\"%.*s\"\n", found, read, tabs*2,"", read, s);

        // inscribe
        env->type = found;
        env->debug = memcpy( calloc(1, read+1), s, read );
        env->parent = parent;

        // patch refs
        if( env->type == '+' && env->prev && env->prev->type == '_' ) {
            int assign_op = ((*s == '=' && s[1] != '=') || (*s != '=' && s[1] == '=')); // include = += -= etc. exclude ==
            int post_op = (*s == s[1] && (*s == '+' || *s == '-')); // v++ v--
            if( assign_op || post_op )
            env->prev->type = '&';
        }
        // patch funcs
        if( env->type == '(' && env->prev && env->prev->type == '_' ) {
            env->prev->type = 'f';
        }

        // parse hierarchy recursively if needed
        int statement = !parent && (found == '#' || found == '{' || found == ';');
        int recursive = found == '#' || found == '{' || found == '(' || found == '[';
        if( recursive ) {
            // save depth
            node *copy_env = env;
            node *copy_parent = parent;

                // ensure subcalls will have current env marked as their parent
                parent = env;

                // inscribe
                env = (env->sibling = calloc(1, sizeof(node)));

                    // recurse
                    ++tabs;
                    parse_string_extended = found == '#';

                        if( found == '#' ) parse( s+1, s+read-1 ); // exclude initial #preprocessor digit from parsing
                        else parse( s+1, s+read-2 );               // exclude initial/ending block{} digits from parsing

                    parse_string_extended = 0;
                    --tabs;

                // relocate sibling list as child
                copy_env->child = copy_env->sibling;
                copy_env->sibling = 0;

            // restore depth
            env = copy_env;
            parent = copy_parent;
        }

        // prepare env for upcoming node
        env->sibling = calloc(1, sizeof(node));

        // linked-list statements when finished
        if( statement ) {
            sta->skip = env->sibling;
            sta = env->sibling;
            if( VERBOSE ) puts("");
        }

        // generate RPN
        {
            array(char*) tokens = 0;
            flat(env, &tokens);
            // for (int i = 0; i < array_count(tokens); ++i) printf("\t%s\n", tokens[i]);
            env->rpn = rpn(tokens);

            // postfix
            strcatf(&env->debug_rpn, "%s", "postfix ");
            for( int i = 0; i < array_count(env->rpn); ++i ) strcatf(&env->debug_rpn, " %s" + !i, env->rpn[i], env->rpn[i][-1]);
            strcatf(&env->debug_rpn, "%s", " << ");
            for( int i = 0; i < array_count(env->rpn); ++i ) strcatf(&env->debug_rpn, "%c", env->rpn[i][-1]);
            strcatf(&env->debug_rpn, "%s", " << ");
            for( int i = 0; i < array_count(tokens); ++i ) strcatf(&env->debug_rpn, "%c", tokens[i][0] );
            strcatf(&env->debug_rpn, "%s", " << infix ");
            for( int i = 0; i < array_count(tokens); ++i ) strcatf(&env->debug_rpn, "%s", 1+tokens[i] );

            if( VERBOSE ) printf(";; vm %s\n", env->debug_rpn + sizeof("postfix ") - 1);
        }

        // linked-list env
        env->sibling->prev = env;
        env = env->sibling;
    }

    return env_begin; // (int)(s - begin);
}

// -----------------------------------------------------------------------------
#line 1 "var.c"

enum {
    TYPE_VOID,TYPE_NULL,TYPE_BOOL,TYPE_CHAR,TYPE_INT,TYPE_FLOAT,TYPE_STRING,TYPE_LIST,TYPE_REF,

    // <-- [insert new types at this point in the enumeration]

    TYPE_FUNCTION, // function call
    TYPE_FFI_CALL, // extern FFI calls
    TYPE_FFI_CALL_DD = TYPE_FFI_CALL,TYPE_FFI_CALL_II,TYPE_FFI_CALL_IS,
};

#define TYPEOFS {"VOID", "NULL", "BOOL", "CHAR", "INT", "FLOAT", "STRING", "LIST", "REF", "FUNCTION", "FFI_CALL"}
#define SIZES   {0, 0, 1, 4, sizeof(Error.d), sizeof(Error.d), -1, -1, 0, sizeof(Error.call), sizeof(Error.ffi_call_dd) }

typedef struct var {
    unsigned type;
    union {
        uintptr_t any;       // 'void*', 'null'
        double d;            // 'bool', 'char', 'int32', 'float' and 'double'
        char* s;             // 'string'

        array(struct var) l; // 'list'
        // map(struct var *, struct var *) m; // 'map'

        union {
            // internal calls
            struct var (*call)(int argc, struct var* argv);
            // extern ffi calls
            double (*ffi_call_dd)(double);
            int (*ffi_call_ii)(int);
            int (*ffi_call_is)(char*);
        };
    };
} var;

#define Void  ((var){ TYPE_VOID, .s = "" })
#define Error ((var){ TYPE_NULL, .s = "NULL(ERROR)" })
#define False ((var){ TYPE_BOOL, .d = 0 })
#define True  ((var){ TYPE_BOOL, .d = 1 })

char* as_string(var x) { // @fixme: return ((var){TYPE_STRING, .s = ...}); instead
    if(x.type==TYPE_VOID)     return va("");
    if(x.type==TYPE_NULL)     return va("%s", "(null)");
    if(x.type==TYPE_BOOL)     return va("%s", x.d ? "true" : "false");
    if(x.type==TYPE_CHAR)     return x.d < 32 ? va("\\x%02x", (char)x.d) : x.d < 127 ? va("%c", (char)x.d) : va("\\u%04x", (int)x.d);
    if(x.type==TYPE_INT)      return va("%"PRIi64, (int64_t)x.d);
    if(x.type==TYPE_LIST)     {
        static __thread char *out = 0; if( out ) *out = 0;
        const char *sep = "";
        for( int i = 0, end = array_count(x.l); i < end; ++ i ) {
            strcatf(&out, "%s%s", sep, as_string(x.l[i]));
            sep = ",";
        }
        return va("[%s]", out ? out : "");
    }
    if(x.type==TYPE_FLOAT)    return va("%f", x.d); // %-9g
    if(x.type==TYPE_STRING)   return va("%s", x.s ? x.s : "(null)");
    if(x.type==TYPE_REF)      return va(".%s" + !x.s, x.s ? x.s : "(null)" );
    if(x.type>=TYPE_FUNCTION) return va("%p", x.call);
    return Error.s;
}
char* pretty_(var x) {
    if(x.type==TYPE_CHAR)     return va("\'%s\'", as_string(x));
    if(x.type==TYPE_STRING)   return va("\"%s\"", as_string(x));
    if(x.type==TYPE_LIST)     {
        static __thread char *out = 0; if( out ) *out = 0;
        const char *sep = "";
        for( int i = 0, end = array_count(x.l); i < end; ++ i ) {
            strcatf(&out, "%s%s", sep, pretty_(x.l[i]));
            sep = ",";
        }
        return va("[%s]", out ? out : "");
    }
    return as_string(x);
}
var len_(int argc, var* x) {
    if( argc != 1 ) return Error;
    int sizes[] = SIZES;
    if(x->type==TYPE_LIST)     return (var){ TYPE_INT, .d = !!x->l * array_count(x->l) };
    if(x->type==TYPE_STRING)   return (var){ TYPE_INT, .d = !!x->s * strlen(x->s) };
    return ((var){TYPE_INT, .d = sizes[x->type]});
}
var typeof_(int argc, var* x) {
    if( argc != 1 ) return Error;
    const char *typeofs[] = TYPEOFS;
    return ((var){TYPE_STRING, .s = (char*)typeofs[x->type]});
}
var print_(int argc, var *argv) {
    for(int i = 0; i < argc; ++i ) {
        puts( as_string(argv[i]) );
    }
    return True;
}
var index_(int argc, var* x) {
    if( argc != 2 ) return Error;
    var *obj = &x[0];
    var *index = &x[1];
    if( index->type == TYPE_INT || index->type == TYPE_FLOAT || index->type == TYPE_CHAR ) {
        if( obj->type==TYPE_LIST ) {
            // convert any positive/negative index into a positive form
            int len = !!obj->l * array_count(obj->l), pos = (int)index->d;
            int indexPositive = pos && len ? (pos > 0 ? pos % len : len-1 + ((pos+1) % len)) : 0;
            return obj->l[ indexPositive ];
        }
        if( obj->type==TYPE_STRING ) {
            // convert any positive/negative index into a positive form
            int len = !!obj->s * strlen(obj->s), pos = (int)index->d;
            int indexPositive = pos && len ? (pos > 0 ? pos % len : len-1 + ((pos+1) % len)) : 0;
            return (var){ TYPE_CHAR, .d = obj->s[indexPositive] };
        }
    }
    return Error;
}
var bool_(int argc, var *x) {
    if( argc != 1 ) return Error;
    if(x->type==TYPE_BOOL)     return *x;
    if(x->type==TYPE_CHAR)     return (var){ TYPE_BOOL, .d = !!(x->d) };
    if(x->type==TYPE_INT)      return (var){ TYPE_BOOL, .d = !!(x->d) };
    if(x->type==TYPE_LIST)     return (var){ TYPE_BOOL, .d = !!(!!x->l * array_count(x->l)) };
    if(x->type==TYPE_FLOAT)    return (var){ TYPE_BOOL, .d = !!(x->d) };
    if(x->type==TYPE_STRING)   return (var){ TYPE_BOOL, .d = !!(!!x->s * !!x->s[0]) };
    if(x->type==TYPE_REF)      return (var){ TYPE_BOOL, .d = !!(!!x->s * !!x->s[0]) }; // @fixme: find_ptr(x->s)
    if(x->type>=TYPE_FUNCTION) return (var){ TYPE_BOOL, .d = !!(x->call) };
    return Error;
}
var string_(int argc, var *x) {
    if( argc != 1 ) return Error;
    return ((var){ TYPE_STRING, .s = as_string(*x) });
}
var char_(int argc, var *x) {
    if( argc != 1 ) return Error;
    return ((var){ TYPE_CHAR, .d = as_string(*x)[0] });
}
var assert_(int argc, var *argv) {
    for(int i = 0; i < argc; ++i ) {
        if( !bool_(1, &argv[i]).d ) {
            exit(-printf("assertion failed: %s %s\n", typeof_(1, &argv[i]).s, pretty_(argv[i])));
            // return False;
        }
    }
    return True;
}

// -----------------------------------------------------------------------------
#line 1 "env.c"

// symbols
array(var) sym[128] = {0};
var* env_find(const char *k) {
    array(var) *depot = &sym[k[0]-'@'];
    for( int i = 0; i < array_count(*depot); i += 2 ) {
        if( !strcmp(k, (*depot)[i].s) ) {
            return &(*depot)[i+1];
        }
    }
    return 0;
}
var* env_insert(const char *k, var v) {
    var *found = env_find(k);
    if( !found ) {
        array(var) *depot = &sym[k[0]-'@'];
        array_push(*depot, ((var){TYPE_STRING, .s=strdup(k)}));
        array_push(*depot, v);
        return array_back(*depot);
    }
    return found;
}
var env_update(const char *k, var v) {
    array(var) *depot = &sym[k[0]-'@'];
    for( int i = 0; i < array_count(*depot); i += 2 ) {
        if( !strcmp(k, (*depot)[i].s) ) {
            return (*depot)[i+1] = v;
        }
    }
    array_push(*depot, ((var){TYPE_STRING, .s=strdup(k)}));
    array_push(*depot, v);
    return v;
}

// -----------------------------------------------------------------------------
#line 1 "lib.c"

// conv
var make_number(const char *str) { // parse c_string->number (float,double,hex,integer)
    int is_hex = str[1] == 'x';
    if( is_hex ) return (var) { TYPE_INT, .d = strtol(str, NULL, 16) };
    int is_float = !!strpbrk(str, ".fe"); // @fixme: assumes 'f' at eos or 'e' in between
    if( is_float ) return (var) { TYPE_FLOAT, .d = atof(str) };
    return (var) { TYPE_INT, .d = atoi(str) };
}
var make_char(const char *str) { // parse c_string->char (a,\x20,\uABCD)
    if( str[1-1] == '\\' && str[2-1] == 'x' ) return (var){TYPE_CHAR, .d = strtol(str+3-1, NULL, 16) };
    if( str[1-1] == '\\' && str[2-1] == 'u' ) return (var){TYPE_CHAR, .d = strtol(str+3-1, NULL, 16) };
    if( str[1-1] != '\0' && str[2-1] == '\'') return (var){TYPE_CHAR, .d = str[1-1] };
    return Error;
}
var make_cstring(const char *str, unsigned num_truncated_ending_chars) { // parse c_string->string ('hi',"hi")
    var out = (var){TYPE_STRING, .s = strdup(str) };
    if( num_truncated_ending_chars ) out.s[ strlen(out.s) - num_truncated_ending_chars ] = '\0';
    return out;
}
var make_fstring(const char *str, unsigned num_truncated_ending_chars) { // parse c_string->f-interpolated string (f'{hi}')
    const char *input = str;
    char *output = "";

    while(*input) {
        char *seek = strchr(input, '{'), *eos = seek ? strchr(seek+1, '}') : 0;
        if( seek && eos ) {
            char *symbol = va("%.*s", (int)(eos-seek-1), seek+1);
            var *z = env_find(symbol);
            if (z) {
                output = va("%s%.*s%s", output, (int)(seek-input), input, as_string(*z));
            } else {
                // @fixme: symbol not found? could it be a {{double moustache}}? if so, eval the inner part
                output = va("%s%.*s{%s}", output, (int)(seek - input), input, symbol);
            }
            input = eos + 1;
        } else {
            if( num_truncated_ending_chars )
                output = va("%s%.*s", output, (int)(strlen(input)-num_truncated_ending_chars), input); // remove ending \"
            else
                output = va("%s%s", output, input);
            break;
        }
    }

    var out = (var){TYPE_STRING, .s = strdup(output) };
    return out;
}
var make_string(const char *str) {
    var out;
    /**/ if( str[0] == 'f' ) out = make_fstring(str+2, 1); // f-interpolated string
    else if( str[0] == '\"') out = make_cstring(str+1, 1); // "string"
    else if( (out = make_char(str+1)).type == Error.type ) out=make_cstring(str+1, 1); // char or 'string'
    return out;
}

// -----------------------------------------------------------------------------
#line 1 "eval.c"

// stack+cpu
var stack[4096]; int sp_ = 0, pc_ = 0;
void clear() {
    pc_ = 0;
    stack[sp_ = 0] = ((var){ TYPE_INT, .d = 0 }); // = Error;
}
void push(var x) {
    stack[sp_+1] = stack[sp_];
    ++sp_;
    stack[sp_-1] = x;
}
void pop(unsigned num) {
    sp_ -= num;
    assert(sp_ >= 0);
}
unsigned pushed() {
    return sp_;
}
var* sp(int i) {
    i = abs(i);
    assert( (sp_ - i) >= 0 );
    return &stack[sp_ - i];
}


var eval_rpn( node *self ) {
    int reg = 0;

    array(char*) tokens = self->rpn;

    if( VERBOSE ) if( self->debug_rpn ) printf(";; vm %s\n", self->debug_rpn + sizeof("postfix ") - 1);

    for( int i = 0; i < array_count(tokens); ++i ) {

        char type = tokens[i][-1];
        char *val = tokens[i];

        /**/ if( type == '0' ) { // number
            push(make_number(val));
        }
        else if( type == '\"' ) { // string
            push(make_string(val));
        }
        else if( type == '&' ) { // reference
            push(((var){TYPE_REF,.s=val}));
        }
        else if( type == '_' ) { // identifier
            var *out = env_find(val);
            push(out ? *out : Error);
        }
        else if( type == 'R' ) { // range i..j[..k]
            int i,j,k = 0;
            sscanf(val, "%d..%d..%d", &i, &j, &k);
            array(var) list = 0;
            if( i < j ) { for( k = abs(k ? k :  1); i <= j; i += k) array_push(list, ((var){ TYPE_INT, .d = i })); }
                   else { for( k = abs(k ? k : -1); i >= j; i -= k) array_push(list, ((var){ TYPE_INT, .d = i })); }
            if( array_back(list)->d != j ) array_push(list, ((var){ TYPE_INT, .d = j }));
            push( ((var){ TYPE_LIST, .l = list }) );
        }
        else if( *val == '.' || (*val == '-' && val[1] == '>') ) {}
        else if( *val == ',' ) { // @fixme: type '+' instead of '@'

            int count = pushed();
            var *argv = &stack[count-2]; // x,y
            if( argv[0].type == TYPE_LIST ) {
                array_push(argv[0].l, argv[1]);
                pop(1);
            } else {
                array(var) list = 0;
                array_push(list, argv[0]);
                array_push(list, argv[1]);
                pop(2);
                push((var){TYPE_LIST, .l=list});
            }

        }
        else if( *val == '@' ) { // indexing     // @fixme: type '+' instead of '@'
            int count = pushed();
            var *argv = &stack[count-2]; // x,y
            argv[0] = index_(2, argv);
            pop(1);
        }
        else if( *val == '[' ) { // list        // @fixme: type '+' instead of '['
            int pass = 0;
            array(var) list = 0;
            int count = pushed();
            if( count ) {
                var *argv = &stack[count-1]; // x
                if( argv[0].type != TYPE_LIST ) {
                    array_push(list, argv[0]);
                    pop(1);
                } else {
                    pass = 1;
                }
            }
            if(!pass) push( ((var){ TYPE_LIST, .l = list }) );
        }

        else if( type == 'f' ) { // function
            var *fn = env_find(val);
            if(!fn) return Error;

            int argc = 1;
            var *argv = sp(1);
            var *list = sp(1), out;
             if( list->type == TYPE_LIST ) {
                //argv = list->l;
                //argc = array_count(list->l);
            }

            // @todo: expand FFI cases
            /**/ if( fn->type == TYPE_FUNCTION ) out = fn->call( argc, argv );
            else if( fn->type == TYPE_FFI_CALL_II && argc == 1 && argv[0].type == TYPE_INT  )   out = ((var){ TYPE_INT,   .d = fn->ffi_call_ii((int)argv[0].d) });
            else if( fn->type == TYPE_FFI_CALL_II && argc == 1 && argv[0].type == TYPE_FLOAT )  out = ((var){ TYPE_INT,   .d = fn->ffi_call_ii(argv[0].d) });
            else if( fn->type == TYPE_FFI_CALL_II && argc == 1 && argv[0].type == TYPE_STRING ) out = ((var){ TYPE_INT,   .d = fn->ffi_call_ii(atoi(argv[0].s)) });
            else if( fn->type == TYPE_FFI_CALL_DD && argc == 1 && argv[0].type == TYPE_INT  )   out = ((var){ TYPE_FLOAT, .d = fn->ffi_call_dd((int)argv[0].d) });
            else if( fn->type == TYPE_FFI_CALL_DD && argc == 1 && argv[0].type == TYPE_FLOAT )  out = ((var){ TYPE_FLOAT, .d = fn->ffi_call_dd(argv[0].d) });
            else if( fn->type == TYPE_FFI_CALL_DD && argc == 1 && argv[0].type == TYPE_STRING ) out = ((var){ TYPE_FLOAT, .d = fn->ffi_call_dd(atof(argv[0].s)) });
            else if( fn->type == TYPE_FFI_CALL_IS && argc == 1 && argv[0].type == TYPE_INT  )   out = ((var){ TYPE_INT,   .d = fn->ffi_call_is(va("%d",(int)argv[0].d)) });
            else if( fn->type == TYPE_FFI_CALL_IS && argc == 1 && argv[0].type == TYPE_FLOAT )  out = ((var){ TYPE_INT,   .d = fn->ffi_call_is(va("%f",argv[0].d)) });
            else if( fn->type == TYPE_FFI_CALL_IS && argc == 1 && argv[0].type == TYPE_STRING ) out = ((var){ TYPE_INT,   .d = fn->ffi_call_is(argv[0].s) });
            else return Error;

            pop(1);
            push(out);
        }

        else if( type == '+' ) {
            int count = pushed();
            var *x = &stack[count-2];
            var *y = &stack[count-1];

            if(0);

            // unary
//          else if( *val == '+' && val[1] == '+' ) { *y = env_update(next->debug, ((var){ TYPE_INT, .d = y->d+1 })); /*pop(1);*/ } // ++v
//          else if( *val == '-' && val[1] == '-' ) { *y = env_update(next->debug, ((var){ TYPE_INT, .d = y->d-1 })); /*pop(1);*/ } // --v
            else if( *val == '+' && val[1] == '+' ) { var *z = env_find(y->s), x = *z; ++z->d; *y = x; /*pop(1);*/ } // v++
            else if( *val == '-' && val[1] == '-' ) { var *z = env_find(y->s), x = *z; --z->d; *y = x; /*pop(1);*/ } // v--
            else if( *val == '!' && val[1] == '\0' ) { *y = ((var){ TYPE_INT, .d = !y->d }); /*pop(1);*/ }
            else if( *val == '~' && val[1] == '\0' ) { *y = ((var){ TYPE_INT, .d = ~(int)y->d }); /*pop(1);*/ }

            // binary     @fixme: specialize: str concat, str trim, etc
            #define IFx(op, ...) else if( *val == 0[#op] ) { *x = __VA_ARGS__; pop(1); }
            #define IFv(op, ...) IFx(op, ((var){ __VA_ARGS__ }))
            #define IFd(op)      IFv(op, TYPE_FLOAT, .d = x->d op y->d)
            #define IFdref(op)   IFv(op, TYPE_FLOAT, .d = env_find(x->s)->d op##= y->d)
            #define IFi(op)      IFv(op, TYPE_INT, .d = (int)x->d op (int)y->d)
            #define IFiref(op)   IFv(op, TYPE_INT, .d = env_find(x->s)->d = (int)env_find(x->s)->d op (int)y->d)

            else if( val[1] == '=' ) { if(0);
                IFv(!, TYPE_BOOL, .d = x->any != y->any ) IFv(=, TYPE_BOOL, .d = x->any == y->any )
                IFv(<, TYPE_BOOL, .d = x->any <= y->any ) IFv(>, TYPE_BOOL, .d = x->any >= y->any )
                IFiref(%) IFiref(&) IFiref(|) IFiref(^) IFdref(+) IFdref(-) IFdref(*) IFdref(/)
                else return Error;
            }

            else if( val[0] == val[1] ) { if(0);
                IFi(&) IFi(|) IFv(*, TYPE_FLOAT,.d=pow(x->d,y->d))
                else return Error;
            }

            IFx(=, env_update(x->s, *y) )
            IFv(<, TYPE_BOOL, .d = x->any < y->any)
            IFv(>, TYPE_BOOL, .d = x->any > y->any)
            IFd(+) IFd(-) IFd(*) IFd(/) IFi(%) IFi(|) IFi(&) IFi(^)
            else return Error;
        }

        else exit(-printf("cannot eval token(%s) type(%c)\n", val, type));

        for( int j = VERBOSE && printf(";; stack "); j; j = puts("") )
            for( int i = 0; i < pushed(); ++i )
                printf(",%s" + !i, pretty_(stack[i]) );
    }

    if( pushed() ) {
        var val = *sp(1);
        pop(1);
        if( val.type == TYPE_REF ) {
            var *out = env_find(val.s);
            if(out) return *out;
        }
        return val;
    }

    return Error;
}

// -----------------------------------------------------------------------------
#line 1 "api.c"

var min_dostring(const char *line) {
    line = va("(%s)", line);
    node *n = parse(line, line + strlen(line));

    clear();
    return eval_rpn(n);
}

var min_dofile(const char *file) {
    var out = Error;
    for( FILE *fp = fopen(file, "rb"); fp; fclose(fp), fp = 0) {
        for( char *data = 0; !data; vrealloc(data, 0) ) {
            size_t len = (fseek(fp, 0L, SEEK_END), ftell(fp));
            if( (data = vrealloc(data, len + 1)) != NULL )
            if( fread( (char*)data, !fseek(fp, 0L, SEEK_SET), len, fp ) == len ) {
                data[len] = '\0';
                for( char *sub = data, *sub2; sub && NULL != (sub2 = sub+strcspn(sub, "\r\n")); sub = sub2+1 ) {
                    *sub2 = 0;
                    if( *sub ) out = min_dostring(sub);
                }
            }
        }
    }
    return out;
}

void min_declare_int(const char *k, int value) {
    env_insert(k, ((var){ TYPE_INT, .d = value }) );
}
void min_declare_double(const char *k, double value) {
    env_insert(k, ((var){ TYPE_FLOAT, .d = value }) );
}
void min_declare_string(const char *k, const char *value) {
    env_insert(k, ((var){ TYPE_STRING, .s = strdup(value) }) );
}
void min_declare_function(const char *k, var (func)(int, var*) ) {
    env_insert(k, ((var){ TYPE_FUNCTION, .call = func }));
}
void min_declare_function_c_ii(const char *k, int (func)(int) ) {
    env_insert(k, ((var){ TYPE_FFI_CALL_II, .ffi_call_ii = func }));
}
void min_declare_function_c_dd(const char *k, double (func)(double) ) {
    env_insert(k, ((var){ TYPE_FFI_CALL_DD, .ffi_call_dd = func }));
}
void min_declare_function_c_is(const char *k, int (func)(const char *) ) {
    env_insert(k, ((var){ TYPE_FFI_CALL_IS, .ffi_call_is = (void*)func }));
}

void min_init() {
    clear();
    env_insert("true", True);
    env_insert("false", False);
    min_declare_function("len", len_);
    min_declare_function("index", index_);
    min_declare_function("print", print_);
    min_declare_function("typeof", typeof_);
    min_declare_function("assert", assert_);
    min_declare_function("bool", bool_);
    min_declare_function("char", char_);
    min_declare_function("string", string_);
}

// -----------------------------------------------------------------------------
#line 1 "repl.c"
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#ifdef _WIN32
#define isatty _isatty
#define fileno _fileno
#endif

void repl() {
    const int is_tty = isatty(fileno(stdin)); // is stdin a terminal or a pipe/file?
    const char *prompt = ">" + !is_tty, *title = is_tty ? "type ^Z to exit\n" : "";
    for( printf("%s%s", title, prompt); !fflush(stdout) && !feof(stdin); printf("%s", prompt)) {
        char line[256] = {0};
        if( feof(stdin) || fgets(line, sizeof(line), stdin) < 0 ) break;
        int len = strlen(line); while(len && line[len-1] <= 32) line[--len] = '\0';
        if( !line[0] ) continue;

        if( !is_tty ) {
            printf(">%s\n", line );
        }

        time_t before = clock();

            var x = min_dostring(line);
            // on errors, re-evaluate and dump syntax tree to terminal if needed
            if( x.type == Error.type && !VERBOSE ) {
                VERBOSE = 1;
                min_dostring(line);
                VERBOSE = 0;
            }

        printf("%s  (%.3fs)\n", x.type == Error.type ? \
            "?" : va("%s %s", typeof_(1, &x).s, pretty_(x)), (clock() - before)*1. / CLOCKS_PER_SEC);
    }
}

// -----------------------------------------------------------------------------
#line 1 "minc.c"

int main(int argc, char **argv) {
    min_init();
    min_declare_string("_BUILD", __DATE__ " " __TIME__);
    min_declare_string("_VERSION", va(__FILE__ " 2022.9(%d)", (int)sizeof(var)));
    min_declare_double("PI", 3.1415926);
    min_declare_function_c_dd("sin", sin);
    min_declare_function_c_dd("cos", cos);
    min_declare_function_c_is("puts", puts);

    time_t before = clock();
        // interactive repl
        if( argc <= 1 ) repl();
        // external files
        for( int i = 1; i < argc; ++i ) {
            var x = min_dofile(argv[i]);
            printf("%s  ", x.type == Error.type ? "?" : va("%s %s", typeof_(1, &x).s, pretty_(x)));
        }
    printf("(%.3fs)\n", (clock() - before)*1. / CLOCKS_PER_SEC);
}
