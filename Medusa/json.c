// json.c - minimal recursive-descent JSON parser (see json.h).

#include "json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

typedef struct {
    const char* p;
} Parser;

static JsonValue* parse_value(Parser* ps);

static void skip_ws(Parser* ps) {
    while (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r') ps->p++;
}

static JsonValue* new_value(JsonType type) {
    JsonValue* v = calloc(1, sizeof(JsonValue));
    v->type = type;
    return v;
}

// Encodes a Unicode code point as UTF-8 into buf, returns bytes written.
static int utf8_encode(unsigned int cp, char* buf) {
    if (cp <= 0x7F) {
        buf[0] = (char)cp;
        return 1;
    } else if (cp <= 0x7FF) {
        buf[0] = (char)(0xC0 | (cp >> 6));
        buf[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    } else {
        buf[0] = (char)(0xE0 | (cp >> 12));
        buf[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        buf[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
}

static char* parse_string_raw(Parser* ps) {
    if (*ps->p != '"') return NULL;
    ps->p++;

    size_t cap = 32, len = 0;
    char* out = malloc(cap);

    while (*ps->p && *ps->p != '"') {
        char c = *ps->p;
        char chunk[4];
        int chunk_len = 0;

        if (c == '\\') {
            ps->p++;
            switch (*ps->p) {
                case '"':  chunk[0] = '"';  chunk_len = 1; break;
                case '\\': chunk[0] = '\\'; chunk_len = 1; break;
                case '/':  chunk[0] = '/';  chunk_len = 1; break;
                case 'b':  chunk[0] = '\b'; chunk_len = 1; break;
                case 'f':  chunk[0] = '\f'; chunk_len = 1; break;
                case 'n':  chunk[0] = '\n'; chunk_len = 1; break;
                case 'r':  chunk[0] = '\r'; chunk_len = 1; break;
                case 't':  chunk[0] = '\t'; chunk_len = 1; break;
                case 'u': {
                    unsigned int cp = 0;
                    for (int i = 0; i < 4 && ps->p[1]; i++) {
                        ps->p++;
                        char h = *ps->p;
                        cp <<= 4;
                        if (h >= '0' && h <= '9') cp |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') cp |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') cp |= (unsigned)(h - 'A' + 10);
                    }
                    chunk_len = utf8_encode(cp, chunk);
                    break;
                }
                default:
                    chunk[0] = *ps->p;
                    chunk_len = 1;
                    break;
            }
            ps->p++;
        } else {
            chunk[0] = c;
            chunk_len = 1;
            ps->p++;
        }

        if (len + (size_t)chunk_len + 1 > cap) {
            cap *= 2;
            out = realloc(out, cap);
        }
        memcpy(out + len, chunk, (size_t)chunk_len);
        len += (size_t)chunk_len;
    }

    if (*ps->p == '"') ps->p++;
    out[len] = '\0';
    return out;
}

static JsonValue* parse_string(Parser* ps) {
    char* s = parse_string_raw(ps);
    if (!s) return NULL;
    JsonValue* v = new_value(JSON_STRING);
    v->as.string = s;
    return v;
}

static JsonValue* parse_number(Parser* ps) {
    char* end;
    double n = strtod(ps->p, &end);
    if (end == ps->p) return NULL;
    ps->p = end;
    JsonValue* v = new_value(JSON_NUMBER);
    v->as.number = n;
    return v;
}

static JsonValue* parse_object(Parser* ps) {
    ps->p++; // '{'
    JsonValue* v = new_value(JSON_OBJECT);
    size_t cap = 8;
    v->as.object.members = malloc(cap * sizeof(JsonMember));
    v->as.object.count = 0;

    skip_ws(ps);
    if (*ps->p == '}') { ps->p++; return v; }

    while (1) {
        skip_ws(ps);
        char* key = parse_string_raw(ps);
        if (!key) { json_free(v); return NULL; }
        skip_ws(ps);
        if (*ps->p != ':') { free(key); json_free(v); return NULL; }
        ps->p++;
        skip_ws(ps);
        JsonValue* val = parse_value(ps);
        if (!val) { free(key); json_free(v); return NULL; }

        if ((size_t)v->as.object.count >= cap) {
            cap *= 2;
            v->as.object.members = realloc(v->as.object.members, cap * sizeof(JsonMember));
        }
        v->as.object.members[v->as.object.count].key = key;
        v->as.object.members[v->as.object.count].value = val;
        v->as.object.count++;

        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == '}') { ps->p++; break; }
        json_free(v);
        return NULL;
    }
    return v;
}

static JsonValue* parse_array(Parser* ps) {
    ps->p++; // '['
    JsonValue* v = new_value(JSON_ARRAY);
    size_t cap = 8;
    v->as.array.items = malloc(cap * sizeof(JsonValue*));
    v->as.array.count = 0;

    skip_ws(ps);
    if (*ps->p == ']') { ps->p++; return v; }

    while (1) {
        skip_ws(ps);
        JsonValue* item = parse_value(ps);
        if (!item) { json_free(v); return NULL; }

        if ((size_t)v->as.array.count >= cap) {
            cap *= 2;
            v->as.array.items = realloc(v->as.array.items, cap * sizeof(JsonValue*));
        }
        v->as.array.items[v->as.array.count++] = item;

        skip_ws(ps);
        if (*ps->p == ',') { ps->p++; continue; }
        if (*ps->p == ']') { ps->p++; break; }
        json_free(v);
        return NULL;
    }
    return v;
}

static JsonValue* parse_value(Parser* ps) {
    skip_ws(ps);
    char c = *ps->p;
    if (c == '"') return parse_string(ps);
    if (c == '{') return parse_object(ps);
    if (c == '[') return parse_array(ps);
    if (c == '-' || isdigit((unsigned char)c)) return parse_number(ps);
    if (strncmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        JsonValue* v = new_value(JSON_BOOL);
        v->as.boolean = true;
        return v;
    }
    if (strncmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        JsonValue* v = new_value(JSON_BOOL);
        v->as.boolean = false;
        return v;
    }
    if (strncmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return new_value(JSON_NULL);
    }
    return NULL;
}

JsonValue* json_parse(const char* text) {
    if (!text) return NULL;
    Parser ps = { .p = text };
    JsonValue* v = parse_value(&ps);
    return v;
}

void json_free(JsonValue* v) {
    if (!v) return;
    switch (v->type) {
        case JSON_STRING:
            free(v->as.string);
            break;
        case JSON_ARRAY:
            for (int i = 0; i < v->as.array.count; i++) json_free(v->as.array.items[i]);
            free(v->as.array.items);
            break;
        case JSON_OBJECT:
            for (int i = 0; i < v->as.object.count; i++) {
                free(v->as.object.members[i].key);
                json_free(v->as.object.members[i].value);
            }
            free(v->as.object.members);
            break;
        default:
            break;
    }
    free(v);
}

const JsonValue* json_get(const JsonValue* obj, const char* key) {
    if (!obj || obj->type != JSON_OBJECT) return NULL;
    for (int i = 0; i < obj->as.object.count; i++) {
        if (strcmp(obj->as.object.members[i].key, key) == 0) return obj->as.object.members[i].value;
    }
    return NULL;
}

const char* json_get_string(const JsonValue* obj, const char* key, const char* def) {
    const JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_STRING) return def;
    return v->as.string;
}

double json_get_number(const JsonValue* obj, const char* key, double def) {
    const JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_NUMBER) return def;
    return v->as.number;
}

bool json_get_bool(const JsonValue* obj, const char* key, bool def) {
    const JsonValue* v = json_get(obj, key);
    if (!v || v->type != JSON_BOOL) return def;
    return v->as.boolean;
}

int json_array_count(const JsonValue* arr) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->as.array.count;
}

const JsonValue* json_array_at(const JsonValue* arr, int idx) {
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    if (idx < 0 || idx >= arr->as.array.count) return NULL;
    return arr->as.array.items[idx];
}
