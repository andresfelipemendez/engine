#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <string.h>
#include <stdbool.h>

typedef struct String {
    const char* data;
    size_t length;
} String;

static inline String String_cstr(const char* in){
    String s = { in, in ? strlen(in):0 };
    return s;
}

#define S(lit) ((String){ lit, sizeof(lit) - 1})

#define S_EMPTY ((String){ NULL, 0 })

static inline bool String_equal(String a, String b) {
    if(a.length != b.length) return false;
    if(!a.data || !b.data) return false;
    return memcmp(a.data, b.data, a.length) == 0;
}

static inline size_t String_to_buf(String s, char* buf, size_t bufcap) {
    if(s.length >= bufcap) s.length = bufcap - 1;
    memcpy(buf, s.data, s.length);
    buf[s.length] = '\0';
    return s.length;
}

#endif