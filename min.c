// min.c scripting language: reference implementation (WIP! WIP! WIP!)
// - rlyeh, public domain.

int VERBOSE = 0; // 0|no debug info, ~0|any debug info, 1|parser, 2|infix, 4|bytecode, 8|stack,

#line 1 "utils.h" // -----------------------------------------------------------

#include <math.h>
#include <time.h>
#include <ctype.h>
#include <assert.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <setjmp.h>

// macros ----------------------------------------------------------------------

#ifdef _MSC_VER
#define __thread __declspec(thread)
#elif defined __TINYC__
#define __thread
#endif

#define do_once static int once = 0; for(;!once;once=1)

// hashes ----------------------------------------------------------------------

uint64_t hash_u64(uint64_t x) {
    x = (x ^ (x >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    x = (x ^ (x >> 27)) * UINT64_C(0x94d049bb133111eb);
    return x ^ (x >> 31);
}
uint64_t hash_flt(double x) {
    union { double d; uint64_t i; } c;
    return c.d = x, hash_u64(c.i);
}
uint64_t hash_str(const char* str) { // @fixme: maybe limit to 32chars?
    uint64_t hash = UINT64_C(14695981039346656037); // hash(0),mul(131) faster than fnv1a, a few more collisions though
    while( *str ) hash = ( (unsigned char)*str++ ^ hash ) * UINT64_C(0x100000001b3);
    return hash;
}
uint64_t hash_ptr(const void *ptr) {
    return hash_u64( (uint64_t)(uintptr_t)ptr ); // @fixme: >>3 needed?
}

// memory leaks ----------------------------------------------------------------

#if defined REPORT_MEMORY_LEAKS && REPORT_MEMORY_LEAKS
#define vrealloc(ptr,len) memleak(vrealloc(memleak(ptr,0,0,0,0),len),len,__func__,__FILE__,__LINE__)
void  memreport(void) { system("type 0*.mem 2> nul > report.mem && del 0*.mem && type report.mem | sort && find /V /C \"\" report.mem"); } // cat 0*.mem > report.mem ; rm 0*.mem ; cat report.mem | sort ; wc -l report.mem
void* memleak(void *ptr, int len, const char *func, const char *file, int line) {
    do_once system("del *.mem 2> nul"), atexit(memreport);
    char n[sizeof(void*)*2+8] = {0}; sprintf(n,"%p.mem",ptr);
    for(FILE *fp = line ? fopen(n,"wb") : 0; fp; fprintf(fp, "%8d bytes, %-20s, %s:%d\n", len,func,file,line), fclose(fp), fp = 0) {}
    return line ? ptr : (unlink(n), ptr);
}
#endif

// memory (vector based allocator; x1.75 enlarge factor) -----------------------

static __thread size_t vstats = 0;
void* (vrealloc)( void* p, size_t sz ) {
    size_t *ret;
    if( !sz ) {
        if( p ) {
            ret = (size_t*)p - 2; vstats -= sizeof(size_t) * 2 + ret[1];
            ret[0] = 0;
            ret[1] = 0;
            ret = (size_t*)realloc( ret, 0 );
        }
        return 0;
    } else {
        if( !p ) {
            ret = (size_t*)realloc( 0, sizeof(size_t) * 2 + sz ); vstats += sizeof(size_t) * 2 + sz;
            ret[0] = sz;
            ret[1] = 0;
        } else {
            ret = (size_t*)p - 2;
            size_t osz = ret[0], cap = ret[1]; // original size and capacity
            if( sz == (size_t)-1 ) return (void*)osz;
            if( sz <= (osz + cap) ) {
                ret[0] = sz;
                ret[1] = cap - (sz - osz);
            } else {
                ret = (size_t*)realloc( ret, sizeof(size_t) * 2 + sz * 1.75 );
                ret[0] = sz;
                ret[1] = (size_t)(sz * 1.75) - sz; vstats += ret[1] - osz;
            }
        }
        return &ret[2];
    }
}
size_t vlen( void* p ) {
    return p == (void*)-1 ? vstats : p ? (size_t)(vrealloc)( p, (size_t)-1 ) : 0; // @addme to fwk
}

#define strdup(s)         (strcpy(vrealloc(0,strlen(s)+1),s))

// arrays ----------------------------------------------------------------------

#define array(t) t*
#define array_resize(t, n) ( array_c_ = array_count(t), array_n_ = (n), array_realloc_((t),array_n_), (array_n_>array_c_? memset(array_c_+(t),0,(array_n_-array_c_)*sizeof(0[t])) : (void*)0), (t) )
#define array_push(t, ...) ( array_realloc_((t),array_count(t)+1), (t)[ array_count(t) - 1 ] = (__VA_ARGS__) )
#define array_pop(t) ( array_c_ = array_count(t), array_c_ ? array_realloc_((t), array_c_-1) : array_cast_(t)(NULL) ) // @addme to fwk
#define array_back(t) ( &(t)[ array_count(t)-1 ] )
#define array_count(t) (int)( (t) ? ( vlen(t) - sizeof(0[t]) ) / sizeof(0[t]) : 0u )
#define array_free(t) ( array_realloc_((t),-1), (t) = 0 ) // -1
#define array_realloc_(t,n)  ( (t) = array_cast_(t) vrealloc((t), ((n)+1) * sizeof(0[t])) ) // +1
#define array_cast_(x) (void *) // cpp: (decltype x)

static __thread unsigned array_c_, array_n_;

// strings ---------------------------------------------------------------------

char* va(const char *fmt, ...) {
    va_list vl, copy;
    va_start(vl, fmt);

        va_copy(copy, vl);
        int sz = /*stbsp_*/vsnprintf( 0, 0, fmt, copy ) + 1;
        va_end(copy);

        enum { STACK_ALLOC = 64*1024 }; assert(sz < STACK_ALLOC && "no stack enough, increase STACK_ALLOC value");
        static __thread char *buf = 0; if(!buf) buf = (vrealloc)(0, STACK_ALLOC); // @leak
        static __thread int cur = 0, len = STACK_ALLOC - 1;

        char* ptr = buf + (cur *= (cur+sz) < len, (cur += sz) - sz);
        /*stbsp_*/vsnprintf( ptr, sz, fmt, vl );

    va_end(vl);
    return ptr;
}
char* append(char **src_, const char *buf) {
    if( !buf || !buf[0] ) return *src_;
    int srclen = (*src_ ? strlen(*src_) : 0), buflen = strlen(buf);
    char *src = (char*)vrealloc(*src_, srclen + buflen + 1 );
    memcpy(src + srclen, buf, buflen + 1 );
    return *src_ = src, src;
}
char *replace(char **copy, const char *target, const char *replacement) { // replaced only if replacement is shorter than target
    int rlen = strlen(replacement), diff = strlen(target) - rlen;
    if( diff >= 0 )
    for( char *s = *copy, *e = s + strlen(*copy); /*s < e &&*/ 0 != (s = strstr(s, target)); ) {
        if( rlen ) s = (char*)memcpy( s, replacement, rlen ) + rlen;
        if( diff ) memmove( s, s + diff, (e - (s + diff)) + 1 );
    }
    return *copy;
}
char* intern(const char* s) {
    static __thread array(char*) depot = 0;
    for(int i = 0; i < array_count(depot); ++i) if( !strcmp(s, depot[i]) ) return depot[i];
    return array_push(depot, strdup(s)), *array_back(depot);
}

#define va(...)             ((printf || printf(__VA_ARGS__), va(__VA_ARGS__)))  // vs2015 check trick
#define append(s,fmt,...)   append((s), va(fmt, __VA_ARGS__))

// maps ------------------------------------------------------------------------

#define map(v)            array(array(v))
#define map_free(m)       do { for(int j=0;j<('z'+1-'A');++j) { for(int i=0;i<array_count(m[j]);i+=2) vrealloc(m[j][i].s, 0); array_free(m[j]); } array_free(m); } /*var_free(m[j][i+1]);*/ while(0)
#define map_find(m,k)     map_find(m,k)
#define map_insert(m,k,v) map_insert((m ? m : array_resize(m,'z'+1-'A'),m),k,v)

// exceptions -----------------------------------------------------------------
// usage: try { puts("before"); throw("my_exception"); puts("after"); } else { puts(thrown); } puts("finally");

#define try         if( !(thrown = 0, setjmp(jmp_ctx)) )
#define throw(...)  longjmp(jmp_ctx, (thrown = 0["" #__VA_ARGS__] ? va("" __VA_ARGS__) : "", 0))
static __thread const char *thrown; static __thread jmp_buf jmp_ctx;

#line 1 "var.h" // -------------------------------------------------------------

enum {
    TYPE_VOID,TYPE_NULL,TYPE_BOOL,TYPE_CHAR,TYPE_FLOAT,TYPE_INT = TYPE_FLOAT,TYPE_STRING,TYPE_LIST,TYPE_REF,TYPE_MAP, // @fixme: bring TYPE_INT back
    // <-- may insert new types here

    TYPE_FUNCTION, // function call
    TYPE_FFI_CALL, // extern FFI calls
    TYPE_FFI_CALL_DD = TYPE_FFI_CALL,TYPE_FFI_CALL_IS,
};

#define TYPEOFS {"VOID", "NULL", "BOOL", "CHAR", "NUM", "STR", "LIST", "REF", "MAP", "FUNC", "FFI"}
#define SIZES   {0, 0, 1, 4, sizeof(False.d), -1, -1, 0, sizeof(Void.m), sizeof(Void.call), sizeof(Void.ffi_call_dd) }

#pragma pack(push,1)
typedef struct var { // 9 bytes
    char type;
    union {
        uintptr_t any;       // 'void*', 'null'
        double d;            // 'bool', 'char', 'int32', 'float' and 'double'
        char* s;             // 'string'
        array(struct var) l; // 'list'
        map(struct var) m;   // 'map'

        union {
            // internal calls
            struct var (*call)(int argc, struct var* argv);
            // extern ffi calls
            double (*ffi_call_dd)(double);
            int (*ffi_call_is)(char*);
        };
    };
} var;
#pragma pack(pop)

#define Void        ((var){ TYPE_VOID, .s = "" })
#define False       ((var){ TYPE_BOOL, .d = 0 })
#define True        ((var){ TYPE_BOOL, .d = 1 })
#define Fail        ((var){ TYPE_NULL, .s = "NULL(ERROR)" })
#define Error(...)  ((var){ TYPE_NULL, .s = "" __VA_ARGS__ /*" L"STRINGIZE(__LINE__)*/ })

#line 1 "env.c" // -------------------------------------------------------------

var* (map_find)(map(var) map, const char *k) {
    array(var) *depot = map + k[0]-'A';
    for( int i = 0; i < array_count(*depot); i+=2 ) if( !strcmp(k+1, (*depot)[i].s+1) ) return &(*depot)[i+1];
    return 0;
}
var* (map_insert)(map(var) map, const char *k, var v) { // technically, map_insert_or_update()
    array(var) *depot = map + k[0]-'A';
    for( int i = 0; i < array_count(*depot); i+=2 ) if( !strcmp(k+1, (*depot)[i].s+1) ) * &(*depot)[i+1] = v, &(*depot)[i+1];
    return array_push(*depot, ((var){TYPE_STRING, .s=strdup(k)})), array_push(*depot, v), array_back(*depot);
}

// globals
static __thread map(var) env;

// vm: stack+cpu
static __thread var stack[4096]; static __thread int sp_, pc_;
void stack_reset() { stack[pc_ = sp_ = 0] = Error("Empty stack"); }
void stack_push(var x) { stack[sp_++] = x; assert(sp_ < 4096); }
void stack_pop(unsigned num) { sp_ -= num; assert(sp_ >= 0); }
int  stack_count() { return sp_; }
var* stack_sp(int i) { return &stack[sp_ - i]; }

#line 1 "lib.c" // -------------------------------------------------------------

#define expand_argv_(x, eq, num) \
    if( !(argc eq num) && argc == 1 && x->type == TYPE_LIST) argc = array_count(x->l), x = x->l; \
    if( !(argc eq num) ) return Error("Wrong number of arguments")

var float_(int argc, var *x) { expand_argv_(x, ==, 1);
    if(x->type==TYPE_BOOL)     return (var){ TYPE_FLOAT, .d = x->d };
    if(x->type==TYPE_CHAR)     return (var){ TYPE_FLOAT, .d = x->d };
    if(x->type==TYPE_FLOAT)    return (var){ TYPE_FLOAT, .d = x->d };
    if(x->type==TYPE_STRING)   return (var){ TYPE_FLOAT, .d = x->s ? atof(x->s) : 0.f };
    return Error("Unsupported type"); // @fixme: TYPE_REF, TYPE_LIST, >=TYPE_FUNCTION
}
var int_(int argc, var *x) {
    return ((var){ TYPE_INT, .d = ((uint64_t)float_(argc, x).d) & UINT64_C(0x1fffffffffffff) });
}
var bool_(int argc, var *x) { expand_argv_(x, ==, 1);
    if(x->type==TYPE_BOOL)     return *x;
    if(x->type==TYPE_CHAR)     return (var){ TYPE_BOOL, .d = !!(x->d) };
    if(x->type==TYPE_LIST)     return (var){ TYPE_BOOL, .d = !!(!!x->l * array_count(x->l)) };
    if(x->type==TYPE_FLOAT)    return (var){ TYPE_BOOL, .d = !!(x->d) };
    if(x->type==TYPE_STRING)   return (var){ TYPE_BOOL, .d = !!(!!x->s * !!x->s[0]) };
    if(x->type==TYPE_REF)      return (var){ TYPE_BOOL, .d = !!(!!x->s * !!x->s[0]) }; // @fixme: map_find(env,x->s)
    if(x->type>=TYPE_FUNCTION) return (var){ TYPE_BOOL, .d = !!(x->call) };
    return Error("Unsupported type");
}
var string_(int argc, var *x) { expand_argv_(x, ==, 1);
    if(x->type==TYPE_VOID)     return ((var){ TYPE_STRING, .s = va("")});
    if(x->type==TYPE_NULL)     return ((var){ TYPE_STRING, .s = va("%s", "(null)")});
    if(x->type==TYPE_BOOL)     return ((var){ TYPE_STRING, .s = va("%s", x->d ? "true" : "false")});
    if(x->type==TYPE_CHAR)     return ((var){ TYPE_STRING, .s = x->d < 32 ? va("\\x%02x", (char)x->d) : x->d < 127 ? va("%c", (char)x->d) : va("\\u%04x", (int)x->d)});
    if(x->type==TYPE_FLOAT)    return ((var){ TYPE_STRING, .s = va("%g", x->d)}); // %f, or va("%"PRIi64, (int64_t)x->d) for ints
    if(x->type==TYPE_STRING)   return ((var){ TYPE_STRING, .s = va("%s", x->s ? x->s : "(null)")});
    if(x->type==TYPE_REF)      return ((var){ TYPE_STRING, .s = x->s ? string_(1, map_find(env,x->s)).s : va("(null)")}); // look-up
    if(x->type>=TYPE_FUNCTION) return ((var){ TYPE_STRING, .s = va("%p", x->call)});
    if(x->type==TYPE_LIST)     {
        static __thread char *out = 0; if( out ) *out = 0;
        for( int i = 0, end = array_count(x->l); i < end; ++ i ) append(&out, ",%s" + !i, string_(1, x->l+i).s);
        return ((var){ TYPE_STRING, .s = va("[%s]", out ? out : "") });
    }
    return Error("Unsupported type");
}
var char_(int argc, var *x) { expand_argv_(x, ==, 1);
    return ((var){ TYPE_CHAR, .d = string_(1, x).s[0] });
}
var quote_(int argc, var *x) { expand_argv_(x, ==, 1);
    if(x->type==TYPE_CHAR)     return ((var){ TYPE_STRING, .s = va("\'%s\'", string_(1, x).s) });
    if(x->type==TYPE_STRING)   return ((var){ TYPE_STRING, .s = va("\"%s\"", string_(1, x).s) });
    if(x->type==TYPE_LIST)     {
        static __thread char *out = 0; if( out ) *out = 0;
        for( int i = 0, end = array_count(x->l); i < end; ++ i ) append(&out, ",%s" + !i, quote_(1, x->l+i).s);
        return ((var){ TYPE_STRING, .s = va("[%s]", out ? out : "") });
    }
    return string_(1, x);
}
var len_(int argc, var* x) { expand_argv_(x, ==, 1);
    int sizes[] = SIZES;
    if(x->type==TYPE_LIST)     return (var){ TYPE_INT, .d = !!x->l * array_count(x->l) };
    if(x->type==TYPE_STRING)   return (var){ TYPE_INT, .d = !!x->s * strlen(x->s) };
    return ((var){TYPE_INT, .d = sizes[x->type]});
}
var typeof_(int argc, var* x) { expand_argv_(x, ==, 1);
    const char *typeofs[] = TYPEOFS;
    if( x->type != TYPE_REF ) return ((var){TYPE_STRING, .s = va("%s", typeofs[x->type])});
    return (var){TYPE_STRING, .s = va("%s%s%s", typeof_(1, map_find(env,x->s)).s, x->s ? " ":"", typeofs[x->type]) };
}
var put_(int argc, var *argv) {
    for(int i = 0; i < argc; ++i ) puts( string_(1, argv+i).s );
    return True;
}
var equal_(int argc, var *x) { expand_argv_(x, ==, 2);
    if( x->type == x[1].type && x->type == TYPE_STRING ) return (var){ TYPE_BOOL, .d = !strcmp(x->s, x[1].s) };
    if( x->type == x[1].type && x->type == TYPE_LIST )   return (var){ TYPE_BOOL, .d = (array_count(x->l) == array_count(x[1].l)) && !memcmp(x->l,x[1].l, array_count(x->l) * sizeof(x->l[0])) };
    return (var){ TYPE_BOOL, .d = x[0].d == x[1].d }; // @fixme: missing types
}
var hash_(int argc, var* x) { expand_argv_(x, ==, 1); // @note: hash truncated to 53-bit, as max lossless uint value in a double is 2^53
    if(x->type==TYPE_BOOL)     return (var){ TYPE_INT, .d = hash_flt(!!x->d) & UINT64_C(0x1fffffffffffff) };
    if(x->type==TYPE_CHAR)     return (var){ TYPE_INT, .d = hash_flt(x->d) & UINT64_C(0x1fffffffffffff) };
    if(x->type==TYPE_INT)      return (var){ TYPE_INT, .d = hash_flt(x->d) & UINT64_C(0x1fffffffffffff) };
    if(x->type==TYPE_FLOAT)    return (var){ TYPE_INT, .d = hash_flt(x->d) & UINT64_C(0x1fffffffffffff) };
    if(x->type==TYPE_STRING)   return (var){ TYPE_INT, .d = (!!x->s * !!x->s[0] * hash_str(x->s)) & UINT64_C(0x1fffffffffffff) };
    if(x->type>=TYPE_FUNCTION) return (var){ TYPE_INT, .d = hash_ptr(x->call) & UINT64_C(0x1fffffffffffff) };
    return Error("Unsupported type"); // @fixme: TYPE_LIST, TYPE_REF
}
var index_(int argc, var* x) { expand_argv_(x, ==, 2); // convert any positive/negative index into a positive form
    int pos = (int)x[1].d, len;
    if( x->type==TYPE_LIST )   return len = !!x->l * array_count(x->l), !len ? Void : x->l[((pos<0)*(len-1) + (pos+(pos<0)) % len)];
    if( x->type==TYPE_STRING ) return len = !!x->s * strlen(x->s), !len ? Void : (var){ TYPE_CHAR, .d = x->s[((pos<0)*(len-1) + (pos+(pos<0)) % len)] };
    return x->type == TYPE_CHAR ? *x : Error("Unsupported type");
}
var eval_(int argc, var *argv) { expand_argv_(argv, ==, 1); // @fixme: remove this limit
    // save context
    int sp2 = sp_;
    var stack2[sizeof(stack)/sizeof(0[stack])]; memcpy(stack2, stack, sizeof(stack));

        const char *min_eval(const char *);
        var out = (min_eval(string_(1, argv).s), *stack_sp(0));

    // restore context
    memcpy(stack, stack2, sizeof(stack));
    sp_ = sp2;

    return out;
}
static __thread int tested_, failed_, line_ = 1; static __thread const char *source_; void summary_(void) { printf("%d/%d tests passed\n", tested_-failed_, tested_); }
var test_(int argc, var *argv) {
    do_once atexit(summary_);
    int errors = failed_;
    for(int i = 0; i < argc; ++i, ++tested_) if(!bool_(1, argv+i).d) printf("Test failed: line %d -- %s\n", line_, source_), ++failed_;
    return errors != failed_ ? False : True;
}
var assert_(int argc, var *argv) {
    for(int i = 0; i < argc; ++i ) if( !bool_(1, argv+i).d ) throw("Assertion failed");
    return True;
}

// conv
uint64_t strtoull_(const char *s, char **end, int b) { uint64_t x; return sscanf(s,"%llx",&x), x; } // strtoull(s,0,16) replacement (tcc)
var make_number(const char *str) { // parse c_string->number (float,double,hex,integer)
    if( str[1] == 'x' )        return (var) { TYPE_INT, .d = strtoull_(str, NULL, 16) }; // is_hex
    if( !strpbrk(str, ".fe") ) return (var) { TYPE_INT, .d = atoi(str) }; // is_int  @fixme: assumes 'f' at eos or 'e' in between floats
    return (var) { TYPE_FLOAT, .d = atof(str) }; // is_float
}
var make_char(const char *str) { // parse c_string->char (a,\x20,\uABCD)
    if( str[0] == '\\' && str[1] == 'x' ) return (var){TYPE_CHAR, .d = strtoull_(str+2, NULL, 16) };
    if( str[0] == '\\' && str[1] == 'u' ) return (var){TYPE_CHAR, .d = strtoull_(str+2, NULL, 16) };
    if( str[0] != '\0' && str[1] == '\'') return (var){TYPE_CHAR, .d = str[0] };
    return Error("Unsupported type");
}
var make_cstring(const char *str, unsigned num_truncated_ending_chars) { // parse c_string->string ('hi',"hi")
    var out = (var){TYPE_STRING, .s = va("%s",str) };
    return num_truncated_ending_chars ? out.s[ strlen(out.s) - num_truncated_ending_chars ] = '\0', out : out;
}
var make_fstring(const char *input, unsigned num_truncated_ending_chars) { // parse c_string->f-interpolated string (f'{hi}',f'{2*3}==6')
    char *output = "";
    while(*input) {
        char *seek = strchr(input, '{'), *eos = seek ? strchr(seek+1, '}') : 0;
        if( !(seek && eos && eos > seek+1) ) break;
        const char *source = va("%.*s", (int)(eos-seek), seek);
        var sym = make_cstring(source+1, 0), *z = (sym = eval_(1, &sym), &sym);
        if( z->type == Fail.type ) { output = va("%s%c",output, *input); input++; continue; }
        output = va("%s%.*s%s", output, (int)(seek-input), input, string_(1, z).s);
        input = eos + 1;
    }
    return (var){TYPE_STRING, .s = va("%s%.*s", output, (int)(strlen(input)-num_truncated_ending_chars), input) }; // remove ending \" if needed
}
var make_string(const char *str) {
    /**/ if( str[0] == 'f' )    return make_fstring(str+2, 1); // f-interpolated string
    else if( str[0] == '\"')    return make_cstring(str+1, 1); // "string"
    var out = make_char(str+1); return out.type != Fail.type ? out : make_cstring(str+1, 1); // char or 'string'
}

#line 1 "rpn.c" // RPN / Shunting-Yard -----------------------------------------

array(char*) rpn(array(char*) tokens) { // [0]1 [+]- [0]2 -> [0]1 [0]2 [+]-
    const char *OPERATORS = // excluded from C: (type){list} compounds, (casts), *indirection, &address-of, sizeof, _Alignof, +-unary
    "\x01`++`\x01`--`"                      // ++ --           Suffix/postfix increment and decrement  Left-to-right
/**/"\x01`@`\x01`[[`" //"\x01`[`\x01`]`"    // []              Array subscripting
    "\x01`.`\x01`->`"                       // . ->            Structure and union member accesses
/**/"\x02`+*`\x02`-*`"                      // ++ --           Prefix increment and decrement  Right-to-left
    "\x02`!`\x02`~`"                        // ! ~             Logical NOT and bitwise NOT
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
    "\x13`?`\x13`:`"                        // ?:              Ternary conditional[note 3] Right-to-left
    "\x14`=`"                               // =               Simple assignment
    "\x14`+=`\x14`-=`"                      // += -=           Assignment by sum and difference
    "\x14`*=`\x14`/=`\x14`%=`"              // *= /= %=        Assignment by product, quotient, and remainder
/**/"\x14`<<=`\x14`>>=`\x14`**=`"           // <<= >>=         Assignment by bitwise left shift and right shift
    "\x14`&=`\x14`^=`\x14`|=`"              // &= ^= |=        Assignment by bitwise AND, XOR, and OR
    "\x15`,`";                              // ,               Comma   Left-to-right

    static __thread int counter = 0; counter = ++counter % 4;
    // stack of indexes to operators
    static __thread array(char*) ss[4] = {0}; array(char*)* s = &ss[counter]; array_resize(*s, 0); // @leak
    // stack of operators (output)
    static __thread array(char*) outs[4] = {0}; array(char*)* out = &outs[counter]; array_resize(*out, 0); // @leak

    for( int i = 0, end = array_count(tokens); i < end; ++i ) {
        char *c = tokens[i];
        // flush pending streams at every statement/keyword
        // else do rpn: functions, ()[], operators and values
        /**/ if( c[-1] == ';' || c[-1] == 'k' ) {
            while (array_count(*s)) {
                const char *token = *array_back(*s);
                array_push(*out, 1+va("%c%s",token[-1],token));
                array_pop(*s);
            }
        }
        else if( c[-1] == 'f' ) { array_push(*s, c); continue; } // functions
        else if( c[-1] == '(' || c[-1] == '[' ) array_push(*s, c[-1] == '(' ? 1+"((" : 1+"[["); // opening xoperators
        else if( c[-1] == ')' || c[-1] == ']' ) { // closing operators
            int open = c[-1] == ')' ? '(' : '[';
            while( array_count(*s) && (*array_back(*s))[-1] != open ) { // until '(' on stack, pop operators.
                const char *token = *array_back(*s);
                array_push(*out, 1+va("%c%s",token[-1],token));
                array_pop(*s);
            }
            if( array_count(*s) ) array_pop(*s); // else perror(unbalanced parentheses)
        } else { // operators
            char *operator = strstr(OPERATORS, va("`%s`", c));
            if( !operator ) { array_push(*out, c); continue; } else ++operator;
            while( array_count(*s) && ((*array_back(*s))[-1] != '(' && (*array_back(*s))[-1] != '[') ) {
                char* back = *array_back(*s);
                int prec2 = operator[-2];
                int prec1 = back[-1] == 'f' ? 0x01 : back[-1] == 'p' ? 0x02 : strstr(OPERATORS, va("`%s`", back))[-1]; // back/s are in %c%s format, not %d`%s`
                int left1 = prec1 != 0x02 && prec1 != 0x13 && prec1 != 0x14;
                if ((100-prec1) > (100-prec2) || (prec2 == prec1 && left1)) {
                    const char *token = *array_back(*s);
                    array_push(*out, 1+va("%c%s",token[-1],token));
                    array_pop(*s);
                } else break;
            }
            array_push(*s, 1+va("+%.*s", (int)(strchr(operator,'`')-operator), operator) );
        }
    }

    // flush pending streams
    while( array_count(*s) ) {
        const char *token = *array_back(*s);
        array_push(*out, 1+va("%c%s",token[-1],token));
        array_pop(*s);
    }

    return *out; // complete
}

#line 1 "parser.c" //-----------------------------------------------------------

static __thread int parse_string_extended; // if true, function below will decode <abc> strings in addition to regular 'abc' and "abc" strings
int parse_string(const char *begin, const char *end) {
    if( *begin == '\'' || *begin == '"' || (parse_string_extended ? *begin == '<' : 0) ) {
        const char *s = begin, *close = *s != '\'' && *s != '"' && parse_string_extended ? ">" : s;
        for( ++s; s <= end && *s != *close; ) s += *s == '\\' && ( s[1] == '\\' || s[1] == *close ) ? 2 : 1;
        return s <= end && *s == *close ? (int)(s + 1 - begin) : 0;
    }
    int ret = *begin == 'f' ? parse_string(begin+1, end) : 0; // f-interpolated strings
    return ret ? ret + 1 : 0;
}

static __thread char found;
int parse_token(const char *begin, const char *end) {
#   define eos (s > end)
    const char *s = begin;
    found = 0;

    if(!found) if((s+=parse_string(s,end))>begin) found = '\"';     // string literals
    if(!found) if( !eos && strchr("{}()[],#",*s)) found = *s++;     // () [] {} sublocks, comma separator and pp
    if(!found) while( !eos && *s == ';'       )   found = ';', ++s; // semi-colons
    if(!found) while( !eos && *s <= ' '  && *s)   found = ' ', ++s; // whitespaces, tabs & linefeeds
    if(!found) while( !eos && *s == '\\' && (s[1] == '\r' && s[2] == '\n') ) found = ' ', s+=3; // pp line
    if(!found) while( !eos && *s == '\\' && (s[1] == '\r' || s[1] == '\n') ) found = ' ', s+=2; // pp line
    if(!found) while( !eos && *s == '/' && s[1] == '*' ) { for(s+=2;!eos && (*s!='*'||s[1]!='/');++s); found='/',s+=2;if(eos)s=end; } // C comments
    if(!found) if( !eos && *s == '/' && s[1] == '/' ) { while( !eos && (*s >= 32) ) ++s; found = '/'; } // C++ comments
    if(!found) if( !eos && *s == '.' && (s[1] == '_' || isalpha(s[1])) ) found = '.', ++s; // oop method invoke: 4.len() "hi".len()

    if(!found) if( !eos && (*s == '_' || isalpha(*s)) ) { // keywords and identifiers
        do ++s; while( !eos && (*s == '_' || isalnum(*s)) );
        found = strstr(",if,elif,else,do,while,for,in,return,break,continue,", va(",%.*s,",(int)(s-begin), begin)) ? 'k' : '_';
    }

    if(!found) if(!eos) { // hex, numbers and ranges. @fixme: write a more canonical float parser
        const char *bak = s; s += s[0] == '-';

        if( s[0] == '.' || isdigit(*s) ) {
            found = '0', ++s;

            const char *hex = "ABCDEFabcd.0123456789ef", *fmt = hex + (s[0] == 'x' ? ++s, 0 : 10);  // .3f 1e4
            while( !eos && *s && strchr(fmt, *s) ) {
                if( s[0] == '.' &&  s[1] == '.' ) s += 2 + (s[2] == '-'), found = 'R'; // not a number; a range instead! 3..-10
                if( s[0] == 'e' && (s[1] == '-' || s[1] == '+') ) ++s; // allow 1e-4
                ++s;
            }

            // throw on '123abc' case. also exclude dot from method invokation: 4.say()
            if(*s == '_' || isalpha(*s)) { if( s[-1] != '.' ) throw("Malformed number"); --s; }
        }

        // handle -1..10 negative range case. abort if not a (-)(R), as previously found (-) lexem is an operator instead
        if( *bak == '-' && found != 'R' ) found = 0, s = bak;
    }

    if(!found) if( !eos && strchr("<=>!&|~^+-/*%.?:\\@", *s) ) { // single/multi digit operators
        /**/ if( s[1] == *s  && s[2] == '=' && strchr("*<>", *s)) s+=3; // **= <<= >>=
        else if( s[1] == *s  && strchr("&|+-*<>", *s) )           s+=2; // && || ++ -- ** << >>
        else if( s[1] == '=' && strchr("<=>!&|~^+-/*%", *s) )     s+=2; // <= == >= != &= |= ~= ^= += -= /= *= %=
        else if( s[0] == '-' && s[1] == '>' )                     s+=2; // ->
        else ++s; // @fixme: patch ': ... \r\n' as '{ ... }' here? beware of ?: operator and sli:ces
        found = '+';
    }

    return found ? (int)(s - begin) : 0;
#   undef eos
}

#line 1 "grammar.c" // ---------------------------------------------------------
// @todo: preprocessor(): remove c/cpp comments, merge \r\n -> \n and \\+\n -> \space\space, trim extra \f\v\t\spaces
// @todo: preprocessor(): if/n/def elif define defined, replace defines, __FILE__, __LINE__, line 1 file, __COUNTER__, STRINGIZE

static __thread const char *src, *ends;

int see(const char *word) {
    int wordlen = strlen(word);
    const char *next = src + strspn(src, " \t\r\n"); // @fixme: missing parse_comment()
    return !strncmp(next, word, wordlen) && (isalnum(word[wordlen-1]) ? !isalnum(next[wordlen]) : 1);
}
int match(const char *word) {
    if( !see(word) ) throw("unexpected `%s` token while matching `%s`", src+strspn(src," \t\r\n"), word);
    return src += strspn(src, " \t\r\n") + strlen(word), 1;
}
int try_match(const char *word) {
    return see(word) ? match(word) : 0;
}

static __thread array(char*) infix;

void expression() { // amalgamation of operands and operators that can be reduced into a single value
    if(!*src) throw("unexpected end-of-string found while parsing expression");

    for( int read = 1; *src; src += read ) {
        read = parse_token(src, ends);

        if( !found ) throw("unexpected token %s", src);
        if( found == ' ' || found == '/' ) continue;

        if( VERBOSE & 1 ) printf(";; L%d parse: %c %.*s\n", line_, found, read, src);

        // patch: either: [l,i,s,t] -> [X] as [[(X)   or   var[index] -> [X] as @(X) (depends on previous token)
        if( found == '[' ) {
            int is_index = array_count(infix) && strchr("_\"])", 0[*array_back(infix)-1]);
            array_push(infix, is_index ? 1+"@@" : 1+"+[[" );
        }
        if( try_match("[") ) { read = 0; array_push(infix, 1+"[["); expression(); array_push(infix, 1+"]]"); match("]"); continue; }
        if( try_match("(") ) { read = 0; array_push(infix, 1+"(("); expression(); array_push(infix, 1+"))"); match(")"); continue; }

        if( strchr("_+R0\",.", found) ) { array_push(infix, 1+va("%c%.*s", found, read, src)); continue; }
        if( strchr(")]};{", *src) ) { /*src+=read;*/ return; } // may be ok

        throw("unexpected token %s", read <= 1 ? va("%c", *src) : va("%c `%.*s`", found, read, src));
    }
}

void emit(const char *keyword) {
    array_push(infix, 1+va("k%s", keyword));
    // @todo
    // L    label
    // jeol jump to end of LOOP scope; begin in do/while loop
    // jbol jump to begin of LOOP scope; end in do/while loop
    // jeoi jump to end of IF scope, bypass [elif(cond) {st}]... [else(cond){st}]
    // jeof jump to end of FUNC scope; ebp=ebp[0],pc=ebp[1]
    // jf+2 jump if stack is false: +2 opcode
    // jf-2 jump if stack is false: -2 opcodes
}

void statement() { // mixture of keywords and expressions that cannot be assigned into a value
    // skip ws + comments
    for( int read = parse_token(src, ends); found == ' ' || found == '/'; src += read, read = parse_token(src,ends)) {}

    /**/ if(!*src) return;                                                                                            // <empty statement>
    /**/ if( strchr("}])",*src))        throw("unexpected %c token found", *src);                                     // <malformed input>
    /**/ if(!strchr(";{:bcrfwdi",*src)) expression();                                                                 // <expression>
    else if( try_match(";") )           {}                                                                            // ;
    else if( try_match("{") )           { emit("Lb"); while( *src && !try_match("}") )     statement(); emit("Le"); } // { <statement>... }
    else if( try_match(":") )           { emit("Lb"); while( *src && !strchr("\r\n",*src)) statement(); emit("Le"); } // : <statement>... \r\n\0
    else if( try_match("break") )       emit("jeol");                                                                 // break
    else if( try_match("continue") )    emit("jbol");                                                                 // continue
    else if( try_match("return") )      expression(), emit("jeof");                                                   // return <expression>
    else if( try_match("for") )         expression(), match("in"),    expression(), statement();                      // for <var> in <var> <statement>
    else if( try_match("while") )       expression(), emit("jf+1"),   statement();                                    // while (...) <statement>
    else if( try_match("do") )          statement(),  match("while"), expression(), emit("jf-2");                     // do <statement> while (...)
    else if( try_match("if") ) {        expression(), emit("jf+2"),   statement(),  emit("jeoi");                     // if (...) <statement> [elif(...) <statement>]... [else <statement>]
      while( try_match("elif") )        expression(), emit("jf+2"),   statement(),  emit("jeoi");
         if( try_match("else") )        statement(); }
    else                                expression();                                                                 // <expression>

    array_push(infix, 1+";;");
}

void declaration() { // any statement that references an unknown identifier
    // a; // variables
    // add1(a) { b = a+1 } // functions
    // global variables are in the text segment and they are directly/immutably addressed
    // arguments and local vars (a,b) are both in stack and thus, they are indirectly/variably addressed
    // bp is the base pointer to the stack frame that holds a+b stack. bp[0] is return address; then bp[0]=>a, bp[1]=>b
    //             /--------stack frame---------\
    // [ stack... ][ old BP ][ old PC ][ a ][ b ]
    statement();
}

void program() { // list of declarations + unique entry point
    while(*src) declaration();
}

array(char*) compile() {
    for( int j = (VERBOSE & 2) && printf(";; L%d infix: ", line_); j; j = puts("") )
        for( int i = 0; i < array_count(infix); ++i ) printf(" %s" + !i, infix[i] );

    // apply last minute infix patches
    static __thread array(char*) patched = 0; array_resize(patched, 0); // @leak
    for( int i = 0, end = array_count(infix); i < end; ++i) {
        char *tk = infix[i], *type = tk - 1; // current token+type
        char *prev = array_count(patched) ? *array_back(patched) : 0, *prev_type = prev ? prev-1 : 0; // previous token+type

        // patch pointer access: A->B as A.B
        /**/ if( !strcmp(type, "+->") ) tk[0]='.', tk[1]='\0';
        // patch functions
        else if( *type == '(' && prev && *prev_type == '_' ) *prev_type = 'f';
        // patch unary into binary: -X as 0-X ; cases: -100 a=-100 a+=-100   @fixme: "hi"@-3  @fixed: `-`
        else if( (i<end-1) && !tk[1] && strchr("+-", tk[0]) && (!prev || prev[strlen(prev)-1] == '=' || strchr("([{;,", *prev_type)) )
                array_push(patched, 1+"00");
        // patch refs
        else if( *type == '_' && prev && *prev_type == '+' ) {
            int post_op = (*prev == prev[1] && (*prev == '+' || *prev == '-')); // ++v --v
            if( post_op ) *type = '&', prev[1] = '*'; // also, tag our postfix ++v --v tokens differently (+* -*) since they must be distinct from v++ v-- tokens
        }
        else if( *type == '+' && prev && *prev_type == '_' ) {
            int three_op = (*tk == tk[1] && tk[2] == '=' && strchr("*<>", *tk)); // <<= >>= **=
            int assign_op = (*tk == '=' && tk[1] != '=') || (!strchr("=<>",*tk) && tk[1] == '='); // include = += -= etc. exclude == <= >=
            int post_op = (*tk == tk[1] && (*tk == '+' || *tk == '-')); // v++ v--
            if( three_op || assign_op || post_op ) *prev_type = '&';
        }

        if( tk ) array_push(patched, tk);
    }
    return rpn(patched);
}

#line 1 "eval.c" // ----------------------------------------------------------

var eval_rpn( array(char*) tokens ) {
    for( int j = (VERBOSE & 4) && printf(";; L%d bytec: ", line_); j; j = puts("") )
        for( int i = 0; i < array_count(tokens); ++i ) printf(" %s%s" + !i, tokens[i][-1] == '&' ? "&" : "", tokens[i] );

    for( int tk = 0, end = array_count(tokens); tk < end; ++tk ) {
        char type = tokens[tk][-1], *val = tokens[tk];

        int count = stack_count();
        var *argv1 = &stack[count-1], *y = argv1; // x
        var *argv2 = &stack[count-2], *x = argv2; // x,y

        /**/ if( type == '0' )  stack_push(make_number(val)); // number
        else if( type == '\"')  stack_push(make_string(val)); // string
        else if( type == '&' )  stack_push(((var){TYPE_REF,.s=val})); // reference
        else if( type == '_' ) { var *out = map_find(env,val); if(!out) throw("Variable %s not found", val); stack_push(*out); } // identifier
        else if( type == 'R' ) { // range i..j[..k]
            int i,j,k = 0;
            sscanf(val, "%d..%d..%d", &i, &j, &k);
            array(var) list = 0;
            if( i < j ) { for( k = abs(k ? k :  1); i <= j; i += k) array_push(list, ((var){ TYPE_INT, .d = i })); }
                   else { for( k = abs(k ? k : -1); i >= j; i -= k) array_push(list, ((var){ TYPE_INT, .d = i })); }
            if( array_back(list)->d != j ) array_push(list, ((var){ TYPE_INT, .d = j }));
            stack_push( ((var){ TYPE_LIST, .l = list }) ); // @leak
        }
        else if( *val == '.' ) {}  // @fixme: type '+'->'.'
        else if( *val == ',' ) {   // @fixme: type '+'->','
            if( argv2[0].type == TYPE_LIST ) {
                array_push(argv2[0].l, argv2[1]);
                stack_pop(1);
            } else {
                array(var) list = 0;
                array_push(list, argv2[0]);
                array_push(list, argv2[1]);
                stack_pop(2);
                stack_push((var){TYPE_LIST, .l=list}); // @leak
            }
        }
        else if( *val == '@' ) { // indexing     // @fixme: type '+'->'@'
            argv2[0] = index_(2, argv2);
            stack_pop(1);
        }
        else if( *val == '[' ) { // list         // @fixme: type '+'->'['
            int skip = 0;
            array(var) list = 0;
            if( count ) {
                if( argv1[0].type != TYPE_LIST ) {
                    array_push(list, argv1[0]);
                    stack_pop(1);
                } else skip = 1;
            }
            if(!skip) stack_push( ((var){ TYPE_LIST, .l = list }) ); // @leak
        }
        else if( type == 'k' ) {}  // keyword
        else if( type == 'f' ) { // function
            var *fn = map_find(env,val);
            if(!fn) throw("Function declarations not yet supported");

            int argc = 1;
            var *argv = stack_sp(argc), out;

            // UCS: "hi".index(0) -> "hi" 0 index .  @todo: verify argc >= 3 cases
            if( tk > 0 && tk < (end-1) ? tokens[tk+1][0] == '.' && (tokens[tk-1][0] != ',' && strcmp(tokens[tk-1], "[[")) : 0 ) {
                argc = stack_count(), argv = stack_sp(argc); // @fixme: 100;"hi".index(0) figure out a better way to detect argc
            }

            /**/ if( fn->type == TYPE_FUNCTION ) out = fn->call( argc, argv );
            else if( fn->type == TYPE_FFI_CALL_DD && argc == 1 ) out = ((var){ TYPE_FLOAT, .d = fn->ffi_call_dd(float_(1,argv).d) });
            else if( fn->type == TYPE_FFI_CALL_IS && argc == 1 ) out = ((var){ TYPE_INT,   .d = fn->ffi_call_is(string_(1,argv).s) });
            else throw("Unsupported function type"); // @todo: expand FFI cases

            stack_pop(1);
            stack_push(out);
        }
        else if( type == '+' ) { // operators
            /**/ if( *val == ':' /*&& tokens[i+1][0] == '?'*/ ) { var *w = &stack[count-3]; *w = w->d ? *x : *y; stack_pop(2); ++tk; /*skip next '?' token*/ }

            // specializations
            else if( x->type == TYPE_STRING && y->type == TYPE_STRING ) { if(0);
                else if( *val == '+' ) { append(&x->s, "%s", y->s); stack_pop(1); } // @todo: string+number -> ref+idx
                else if( *val == '-' ) { replace(&x->s, y->s, ""); stack_pop(1); }
                else if( val[1] != '=' ) { if(0);
                else if( *val == '<' ) { *x = ((var){ TYPE_BOOL, .d=strcmp(x->s, y->s) < 0 }); stack_pop(1); }
                else if( *val == '>' ) { *x = ((var){ TYPE_BOOL, .d=strcmp(x->s, y->s) > 0 }); stack_pop(1); }
                else throw("Unknown operator");
                } else { if(0); // @todo: add += -=
                else if( *val == '<' ) { *x = ((var){ TYPE_BOOL, .d=strcmp(x->s, y->s) <= 0 }); stack_pop(1); }
                else if( *val == '!' ) { *x = equal_(2, x), x->d = !x->d; stack_pop(1); }
                else if( *val == '=' ) { *x = equal_(2, x); stack_pop(1); }
                else if( *val == '>' ) { *x = ((var){ TYPE_BOOL, .d=strcmp(x->s, y->s) >= 0 }); stack_pop(1); }
                else throw("Unknown operator");
                }
            }

            else if( x->type == TYPE_LIST && y->type == TYPE_LIST ) { if(0); // @todo: add + - += -= == !=
                else if( val[1] == '=' ) { if(0);
                else if( *val == '!' ) { *x = equal_(2, x); x->d = !x->d; stack_pop(1); }
                else if( *val == '=' ) { *x = equal_(2, x); stack_pop(1); }
                else throw("Unknown operator");
                } else { if(0);
                else throw("Unknown operator");
                }
            }

            // unary
            else if( *val == '+' && val[1] == '*' ) { var *z = map_find(env,y->s); ++z->d; *y = *z; } // ++v
            else if( *val == '-' && val[1] == '*' ) { var *z = map_find(env,y->s); --z->d; *y = *z; } // --v
            else if( *val == '+' && val[1] == '+' ) { var *z = map_find(env,y->s), w = *z; ++z->d; *y = w; } // v++
            else if( *val == '-' && val[1] == '-' ) { var *z = map_find(env,y->s), w = *z; --z->d; *y = w; } // v--
            else if( *val == '!' && val[1] == '\0' ) { *y = ((var){ TYPE_INT, .d = !y->d }); }
            else if( *val == '~' && val[1] == '\0' ) { *y = ((var){ TYPE_INT, .d = ~(int)y->d }); }

            // binary
            #define IFx(op, ...) else if( *val == 0[#op] ) { *x = __VA_ARGS__; stack_pop(1); }
            #define IFv(op, ...) IFx(op, ((var){ __VA_ARGS__ }))
            #define IFd(op)      IFv(op, TYPE_FLOAT, .d = x->d op y->d)
            #define IFdREF(op)   IFv(op, TYPE_FLOAT, .d = map_find(env,x->s)->d op##= y->d)
            #define IFi(op)      IFv(op, TYPE_INT, .d = (int)x->d op (int)y->d)
            #define IFiREF(op)   IFv(op, TYPE_INT, .d = map_find(env,x->s)->d = (int)map_find(env,x->s)->d op (int)y->d)

            else if( val[1] == '=' ) { if(0); // != == <= >= %= &= |= ^= += -= *= /=
                IFx(!, equal_(2,x), x->d=!x->d ) IFx(=, equal_(2,x) ) //IFv(!, TYPE_BOOL, .d = x->d != y->d ) IFv(=, TYPE_BOOL, .d = x->d == y->d )
                IFv(<, TYPE_BOOL, .d = x->d <= y->d ) IFv(>, TYPE_BOOL, .d = x->d >= y->d )
                IFiREF(%) IFiREF(&) IFiREF(|) IFiREF(^) IFdREF(+) IFdREF(-) IFdREF(*) IFdREF(/)
                else throw("Unknown operator");
            }

            else if( val[0] == val[1] && val[2] == '=' ) { if(0); // <<= >>= **=
                IFiREF(<<) IFiREF(>>) else if(*val == '*') { *x = ((var){TYPE_FLOAT, .d = map_find(env,x->s)->d = pow(map_find(env,x->s)->d,y->d)}); stack_pop(1); }
                else throw("Unknown operator");
            }
            else if( val[0] == val[1] ) { if(0); // && || << >> **
                IFi(&&) IFi(||) IFi(<<) IFi(>>) IFv(*, TYPE_FLOAT,.d=pow(x->d,y->d))
                else throw("Unknown operator");
            }

            IFx(=, *map_insert(env, x->s, *y) )
            IFv(<, TYPE_BOOL, .d = x->d < y->d)
            IFv(>, TYPE_BOOL, .d = x->d > y->d)
            IFd(+) IFd(-) IFd(*) IFd(/) IFi(%) IFi(|) IFi(&) IFi(^)
            else throw("Unknown operator");
        }
        else throw("Cannot eval token `%s` type(%c)\n", val, type);

        for( int j = (VERBOSE & 8) /*&& (tk == end-1)*/ && printf(";; L%d stack: ", line_); j; j = puts("") )
            for( int k = 0; k < stack_count(); ++k )
                printf(",%s" + !k, stack[k].type == TYPE_REF ? va("&%s", stack[k].s) : quote_(1,stack+k).s );
    }

    return stack_count() ? stack_pop(1), *stack_sp(-1) : Error("Empty stack");
}

#line 1 "api.c" // -------------------------------------------------------------

const char* min_eval(const char *line) {
    static __thread char *output = 0; if(output) output[0] = 0; // @leak

    try {
        // parse program
        src = source_ = line; ends = line + strlen(line);
        stack_reset();
        array_resize(infix, 0);
        program();

        // compile & run program
        array(char*) code = compile();
        if( array_count(code) ) { // ; else throw("empty expression");
            var *x = (eval_rpn(code), stack_sp(0));
            append(&output, "!Error: %s %s %s" + 8 * (x->type != Fail.type), x->type == Fail.type ? x->s : "", typeof_(1, x).s, quote_(1, x).s);
        }
    }
    else append(&output, "!Error: %s on line %d %s", thrown, line_, source_);

    return output ? output : "";
}

const char* min_file(const char *file) {
    const char *out = "!Cannot read file"; line_ = 0;
    for( FILE *fp = fopen(file, "rb"); fp; fclose(fp), fp = 0 )
        for( size_t len = (fseek(fp, 0L, SEEK_END), ftell(fp)); len; len = 0 )
            for( char *p = vrealloc(0, len + 1); p && (p[len] = 0, p); p = vrealloc(p, 0) )
                for( int read = fread( (char*)p, !fseek(fp, 0L, SEEK_SET), len, fp ) == len; read; read = 0 )
                    for( char *s = p, *s2; s && *s && NULL != (s2 = s+strcspn(s, "\r\n")); )
                        *s2 = 0, ++line_, out = min_eval(s), s=s2+1, s+=*s=='\r', s+=*s=='\n';
    return out;
}

void min_declare_int(const char *k, int value) { map_insert(env, k, ((var){ TYPE_INT, .d = value }) ); }
void min_declare_double(const char *k, double value) { map_insert(env, k, ((var){ TYPE_FLOAT, .d = value }) ); }
void min_declare_string(const char *k, const char *value) { map_insert(env, k, ((var){ TYPE_STRING, .s = strdup(value) }) ); } // @leak
void min_declare_function(const char *k, var (func)(int, var*) ) { map_insert(env, k, ((var){ TYPE_FUNCTION, .call = func })); }
void min_declare_function_extern_is(const char *k, int (func)(const char *) ) { map_insert(env, k, ((var){ TYPE_FFI_CALL_IS, .ffi_call_is = (void*)func })); }
void min_declare_function_extern_dd(const char *k, double (func)(double) ) { map_insert(env, k, ((var){ TYPE_FFI_CALL_DD, .ffi_call_dd = func })); }

void min_quit(void) { map_free(env); }
void min_init() { // @todo: add config flags { realloc, dlsym, verbosity }
    int is_asserting = 0; assert( ++is_asserting );
    stack_reset();
    map_insert(env, "true", True); map_insert(env, "false", False);
    min_declare_string("_BUILD", __DATE__ " " __TIME__);
    min_declare_string("_VERSION", va("min.c 2022.10-var%d%s", (int)sizeof(var), is_asserting ? "-DEBUG" : ""));
    min_declare_string("_EXTENSION", ",eval,typeof,");
#   define min_builtin(fn) min_declare_function(#fn, fn##_)
    min_builtin(len);  min_builtin(index);  min_builtin(hash);  min_builtin(put);    min_builtin(assert);  min_builtin(test);
    min_builtin(int);  min_builtin(bool);   min_builtin(char);  min_builtin(float);  min_builtin(string);  min_builtin(quote);
    min_builtin(eval); min_builtin(typeof); min_builtin(equal);
}

#line 1 "bench.c" // -----------------------------------------------------------

int fib(int n) {
    return n < 2 ? n : fib(n-1) + fib(n-2);
}

void benchmarks() {
    clock_t t0;

#   define benchmark(s,...) (puts(s), t0 = -clock(), __VA_ARGS__, t0 += clock(), t0*1. / CLOCKS_PER_SEC)
    double native = benchmark("Running native fib(40)...", fib(40));
    double vm = benchmark("Running min.vm fib(40)...", min_eval("fib(n) { n < 2 ? n : fib(n-1) + fib(n-2) } fib(40)")); // @fixme

    const char *vendors[] = { // using latest versions as of Sep 2022
        va("%f c(native)", native), va("%f min.vm(broken)", vm),
        "0.59 cl-19.31","1.21 tcc-0.9.27","1.34 twok","1.94 luajit-2.1","2.39 c4x86",
        "23.96 lua-5.4","30.35 quickjs-2021","32.7 wren","42.69 xc","51.54 python-3.10",
        "63.4 c4","71.3 ape","NAN little","82.40 duktape-2.7.0","872.28 mjs-2021","172800 chaiscript-2022" // computer crashed after running chaiscript for 48 hours... not going to test again.
    };

    char vendor[64];
    double baseline = native, speed;
    for( int i = 0; i < sizeof(vendors)/sizeof(vendors[0]); ++i ) {
        if( sscanf(vendors[i], "%lg %[^\n]", &speed, vendor) != 2 ) continue;
        char *time = va("%10.2fs", speed != speed ? 0. : speed );
        char *info = va("%12.2f%% %s", 100*fabs((speed / baseline)-1), speed != speed ? "broken" : baseline<speed ? "slower" : baseline>speed ? "faster" : "(baseline)");
        printf("%s %s %16s relative speed is %s\n", speed != speed ? "!" : i <= 1 ? "*" : " ", time, vendor, info);
    }
}

#line 1 "repl.c" // ------------------------------------------------------------

int main(int argc, char **argv) {
    min_init(); atexit(min_quit);
    min_declare_double("PI", 3.1415926);
    min_declare_function_extern_dd("sin", sin);
    min_declare_function_extern_dd("cos", cos);
    min_declare_function_extern_is("puts", puts);

    time_t before = clock();
        // interactive repl
        if( argc <= 1 ) for( printf("type ^Z to exit\n>"); !fflush(stdout) && !feof(stdin); printf(">") ) {
            char line[256] = {0};
            if( feof(stdin) || fgets(line, sizeof(line), stdin) < 0 ) break;
            line[ strcspn(line, "\r\n") ] = '\0'; if( !line[0] ) continue;
            if(line[0] == '!') { if( !strcmp(line, "!V") ) VERBOSE = !VERBOSE * 0xFF; else system(line+1); continue; }
            const char *rc = (before = clock(), min_eval(line));
            printf("%s  (%.3fs, %5.2fKiB)\n", rc, (clock() - before)*1. / CLOCKS_PER_SEC, vlen((void*)-1) / 1024.0);
        }
        // benchmarks, eval string and help flags
        else if( !strcmp(argv[1], "-b") ) benchmarks(), exit(0);
        else if( !strcmp(argv[1], "-e") ) puts(min_eval(argc > 2 ? argv[2] : "")), exit(0);
        else if( !strcmp(argv[1], "-h") ) printf("%s\n%s  \t\t\tconsole\n%s -b\t\t\tbenchmarks\n%s -e \"string...\"\teval string\n%s file1 [file2...]\teval file(s)\n",map_find(env,"_VERSION")->s,argv[0],argv[0],argv[0],argv[0]), exit(0);
        // external files
        else for( int i = 1; i < argc; ++i ) puts(min_file(argv[i]));
    printf("(%.3fs, %5.2fKiB)\n", (clock() - before)*1. / CLOCKS_PER_SEC, vlen((void*)-1) / 1024.0);
}
