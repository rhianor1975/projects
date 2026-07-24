// json.h - minimal JSON parser, just enough to read Jellyfin API responses.
// Not a general-purpose library: no writer, no streaming, no comments.
#ifndef MEDUSA_JSON_H
#define MEDUSA_JSON_H

#include <stdbool.h>

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT,
} JsonType;

typedef struct JsonValue JsonValue;

typedef struct {
    char* key;
    JsonValue* value;
} JsonMember;

struct JsonValue {
    JsonType type;
    union {
        bool boolean;
        double number;
        char* string;
        struct { JsonValue** items; int count; } array;
        struct { JsonMember* members; int count; } object;
    } as;
};

// Parses `text` into a JsonValue tree. Returns NULL on malformed input.
// Caller owns the result and must json_free() it.
JsonValue* json_parse(const char* text);
void json_free(JsonValue* v);

// Accessors. All are safe to call on NULL / wrong-typed values, returning
// the given default - callers don't need to check types defensively.
const JsonValue* json_get(const JsonValue* obj, const char* key);
const char* json_get_string(const JsonValue* obj, const char* key, const char* def);
double json_get_number(const JsonValue* obj, const char* key, double def);
bool json_get_bool(const JsonValue* obj, const char* key, bool def);

int json_array_count(const JsonValue* arr);
const JsonValue* json_array_at(const JsonValue* arr, int idx);

#endif
