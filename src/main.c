#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define global static
#define local_persist static
#define internal static

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT (2 * sizeof(void *))
#endif

#define KB 1024
#define MB (1024 * KB)

// Arena
typedef struct Arena Arena;
struct Arena {
  uint8_t *buf;
  size_t buf_size;
  size_t prev_offset;
  size_t curr_offset;
};

// align will be 2's power
internal uintptr_t align_forward(uintptr_t ptr, size_t align) {
  assert((align & (align - 1)) == 0);

  uintptr_t p = ptr;
  uintptr_t a = align;
  uintptr_t modulo = p % a;

  if (modulo != 0) {
    p = p + (a - modulo);
  }
  return p;
}

internal void arena_init(Arena *a, void *backing_buffer,
                         size_t backing_buffer_size) {
  a->buf = (uint8_t *)backing_buffer;
  a->buf_size = backing_buffer_size;
  a->curr_offset = 0;
  a->prev_offset = 0;
}

internal void *arena_alloc_align(Arena *a, size_t size, size_t align) {
  uintptr_t curr_ptr = (uintptr_t)a->buf + a->curr_offset;
  uintptr_t aligned_ptr = align_forward(curr_ptr, align);
  uintptr_t aligned_offset = aligned_ptr - (uintptr_t)a->buf;

  if (aligned_offset + size <= a->buf_size) {
    a->prev_offset = aligned_offset;
    a->curr_offset = aligned_offset + size;

    // Zero memory
    memset((void *)aligned_ptr, 0, size);
    return (void *)aligned_ptr;
  }
  return NULL;
}

internal void *arena_alloc(Arena *a, size_t size) {
  return arena_alloc_align(a, size, DEFAULT_ALIGNMENT);
}

internal void arena_free_all(Arena *a) {
  a->curr_offset = 0;
  a->prev_offset = 0;
}

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

internal String str_init(char *str, uint64_t size) {
  String result = {.str = (uint8_t *)str, .size = size};
  return result;
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

internal bool str_equal_cstr(String s, char *cstr) {
  String b = str_init(cstr, strlen(cstr));
  return str_equal(s, b);
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

internal StringList str_split_cstr(Arena *a, char *cstr, char *split_chars) {
  String str = str_init(cstr, strlen(cstr));
  String split_chars_str = str_init(split_chars, strlen(split_chars));
  return str_split(a, str, split_chars_str);
}

internal void str_print(String str) {
  if (str.str != NULL) {
    for (int i = 0; i < str.size; i += 1) {
      putchar(str.str[i]);
    }
  }
}

internal void str_list_print(StringList *list) {
  if (list != NULL) {
    StringNode *ptr = list->first;
    for (; ptr != NULL; ptr = ptr->next) {
      str_print(ptr->string);
      printf("\n");
    }
    printf("\n");
  }
}

internal void echo(StringList *cmd) {
  assert(cmd != NULL);
  assert(cmd->node_count > 0);
  assert(cmd->first != NULL);

  StringNode *args = cmd->first->next;
  while (args != NULL) {
    str_print(args->string);
    char split = args->next == NULL ? '\n' : ' ';
    putchar(split);
    args = args->next;
  }
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  uint8_t *arena_backing_buffer = (uint8_t *)malloc(4 * MB);
  Arena arena = {0};
  arena_init(&arena, arena_backing_buffer, 4 * MB);

  while (true) {
    printf("$ ");

    char cmd[1024];
    fgets(cmd, sizeof(cmd), stdin);
    cmd[strcspn(cmd, "\n")] = '\0';

    StringList list = str_split_cstr(&arena, cmd, "\n\t ");

    if (str_equal_cstr(list.first->string, "exit")) {
      break;
    } else if (str_equal_cstr(list.first->string, "echo")) {
      echo(&list);
    } else {
      printf("%s: command not found\n", cmd);
    }
  }

  free(arena_backing_buffer);
  return 0;
}
