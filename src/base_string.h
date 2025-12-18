#ifndef CODECRAFTER_STRING_H
#define CODECRAFTER_STRING_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "arena.h"
#include "base.h"

typedef struct String String;
struct String {
  uint8_t *str;
  uint64_t size;
};

typedef struct StringNode StringNode;
struct StringNode {
  String string;
  StringNode *next;
};

typedef struct StringList StringList;
struct StringList {
  StringNode *first;
  StringNode *last;
  uint64_t node_count;
  uint64_t total_size;
};

typedef struct StringArray StringArray;
struct StringArray {
  String *items;
  uint64_t count;
  uint64_t capacity;
};

internal void str_array_push(Arena *a, StringArray *arr, String str) {
  if (arr->count >= arr->capacity) {
    uint64_t new_cap = arr->capacity == 0 ? 8 : arr->capacity * 2;
    String *new_items = (String *)arena_alloc(a, sizeof(String) * new_cap);
    if (arr->items) {
      memcpy(new_items, arr->items, sizeof(String) * arr->count);
    }
    arr->items = new_items;
    arr->capacity = new_cap;
  }
  arr->items[arr->count++] = str;
}

internal String str_init(const char *str, uint64_t size) {
  String result = {.str = (uint8_t *)str, .size = size};
  return result;
}

internal String str_clone_from_cstring(Arena *a, const char *str,
                                       uint64_t size) {
  uint8_t *buf = (uint8_t *)arena_alloc(a, size);
  memcpy(buf, str, size);
  String string = {.str = buf, .size = size};
  return string;
}

internal char *to_cstring(Arena *a, String s) {
  char *cstr = (char *)arena_alloc(a, s.size + 1);
  memcpy(cstr, s.str, s.size);
  cstr[s.size] = '\0';
  return cstr;
}

internal bool str_equal(String a, String b) {
  bool equal = true;
  if (a.size == b.size) {
    for (int i = 0; i < a.size; i += 1) {
      if (a.str[i] != b.str[i]) {
        equal = false;
        break;
      }
    }
  } else {
    equal = false;
  }
  return equal;
}

internal bool str_is_posnum(String s) {
  bool result = true;
  for (int i = 0; i < s.size; i += 1) {
    if (s.str[i] < '0' || s.str[i] > '9') {
      result = false;
      break;
    }
  }
  return result;
}

internal bool str_equal_cstr(String s, const char *cstr) {
  String b = str_init(cstr, strlen(cstr));
  return str_equal(s, b);
}

internal bool str_starts_with_cstr(String s, const char *cstr) {
  assert(cstr != NULL);

  size_t len = strlen(cstr);
  bool result = true;
  if (len <= s.size) {
    for (int idx = 0; idx < len; idx += 1) {
      if (s.str[idx] != cstr[idx]) {
        result = false;
        break;
      }
    }
  } else {
    result = false;
  }

  return result;
}

internal bool str_ends_with_cstr(String s, const char *cstr) {
  assert(cstr != NULL);

  size_t len = strlen(cstr);
  bool result = true;
  if (len <= s.size) {
    for (int idx = s.size - len; idx < s.size; idx += 1) {
      if (s.str[idx] != cstr[idx]) {
        result = false;
        break;
      }
    }
  } else {
    result = false;
  }

  return result;
}

internal StringNode *str_list_push(Arena *a, StringList *list, String str) {
  StringNode *node = (StringNode *)arena_alloc(a, sizeof(StringNode));
  node->string = str;

  if (list->first == NULL) {
    list->first = node;
  }
  if (list->last != NULL) {
    list->last->next = node;
  }
  list->last = node;
  list->node_count += 1;
  list->total_size += str.size;

  return node;
}

internal StringNode *str_list_push_cstr(Arena *a, StringList *list,
                                        const char *cstr) {
  String str = str_init(cstr, strlen(cstr));
  return str_list_push(a, list, str);
}

internal StringList str_split(Arena *a, String string, String split_chars) {
  StringList list = {0};
  uint8_t *ptr = string.str;
  uint8_t *end = string.str + string.size;

  for (; ptr < end; ptr += 1) {
    uint8_t *first = ptr;
    bool found = false;

    for (; ptr < end; ptr += 1) {
      uint8_t ch = *ptr;

      for (int i = 0; i < split_chars.size; i += 1) {
        if (split_chars.str[i] == ch) {
          found = true;
          break;
        }
      }

      if (found) {
        break;
      }
    }

    String result = {0};
    result.str = first;
    result.size = (uint64_t)(ptr - first);

    if (result.size > 0) {
      str_list_push(a, &list, result);
    }
  }

  return list;
}

internal String str_concat_sep(Arena *a, String s1, String s2, String sep) {
  String result = {0};
  size_t size = s1.size + s2.size + sep.size;
  uint8_t *buf = (uint8_t *)arena_alloc(a, size);

  memcpy(buf, s1.str, s1.size);
  if (sep.size > 0) {
    memcpy(buf + s1.size, sep.str, sep.size);
  }
  memcpy(buf + s1.size + sep.size, s2.str, s2.size);

  result.str = buf;
  result.size = size;
  return result;
}

internal String str_concat(Arena *a, String s1, String s2) {
  String sep = {0};
  return str_concat_sep(a, s1, s2, sep);
}

internal String str_substr(String s, uint64_t start, uint64_t end) {
  assert(start <= end);
  assert(end <= s.size);

  size_t size = end - start;
  String result = {.str = s.str + start, .size = size};
  return result;
}

internal StringList str_split_cstr(Arena *a, char *cstr, char *split_chars) {
  String str = str_init(cstr, strlen(cstr));
  String split_chars_str = str_init(split_chars, strlen(split_chars));
  return str_split(a, str, split_chars_str);
}

internal void str_print(String str) {
  if (str.size > 0) {
    fwrite(str.str, 1, str.size, stdout);
  }
}

internal void str_list_print(StringList *list) {
  if (list != NULL) {
    StringNode *ptr = list->first;
    for (; ptr != NULL; ptr = ptr->next) {
      str_print(ptr->string);
      putchar('\n');
    }
    putchar('\n');
  }
}

#endif
