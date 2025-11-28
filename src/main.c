#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define global static
#define local_persist static
#define internal static

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT (2 * sizeof(void *))
#endif

#define KB 1024
#define MB (1024 * KB)
#define PATH_MAX_LEN 4096

#define SINGLE_QUOTE '\''
#define DOUBLE_QUOTE '"'
#define BACKSLASH '\\'

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

typedef struct TempArenaMemory TempArenaMemory;
struct TempArenaMemory {
  Arena *arena;
  size_t prev_offset;
  size_t curr_offset;
};

TempArenaMemory temp_arena_memory_begin(Arena *a) {
  TempArenaMemory temp = {
      .arena = a,
      .prev_offset = a->prev_offset,
      .curr_offset = a->curr_offset,
  };

  return temp;
}

void temp_arena_memory_end(TempArenaMemory temp) {
  temp.arena->prev_offset = temp.prev_offset;
  temp.arena->curr_offset = temp.curr_offset;
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

internal bool str_equal_cstr(String s, char *cstr) {
  String b = str_init(cstr, strlen(cstr));
  return str_equal(s, b);
}

internal bool str_ends_with_cstr(String s, char *cstr) {
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
                                        char *cstr) {
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
    printf("%.*s", (int)str.size, str.str);
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

typedef struct RedirectInfo RedirectInfo;
struct RedirectInfo {
  int source_fd;
  String output_file_name;
  int flag;
};

typedef struct ShellCommand ShellCommand;
struct ShellCommand {
  String exe;
  StringList args;
  RedirectInfo redir_info;
};

internal void echo(ShellCommand *cmd) {
  assert(cmd->args.total_size > 0);
  assert(cmd->args.first != NULL);

  StringNode *arg = cmd->args.first->next;
  while (arg != NULL) {
    str_print(arg->string);
    char split = arg->next == NULL ? '\n' : ' ';
    putchar(split);
    arg = arg->next;
  }
}

internal bool is_builtin(String cmd, StringList *builtin_cmds) {
  assert(builtin_cmds != NULL);

  bool result = false;
  StringNode *ptr = builtin_cmds->first;
  for (; ptr != NULL; ptr = ptr->next) {
    if (str_equal(cmd, ptr->string)) {
      result = true;
      break;
    }
  }
  return result;
}

internal String search_path(Arena *a, String cmd, StringList *env_path_list) {
  assert(env_path_list != NULL);
  String result = {0};
  String sep = {.str = (uint8_t *)"/", .size = 1};

  TempArenaMemory temp = temp_arena_memory_begin(a);
  char *buffer = (char *)arena_alloc(a, PATH_MAX_LEN);

  StringNode *ptr = env_path_list->first;
  for (; ptr != NULL; ptr = ptr->next) {
    String file_path = {0};
    if (ptr->string.str[ptr->string.size - 1] == '/') {
      file_path = str_concat(a, ptr->string, cmd);
    } else {
      file_path = str_concat_sep(a, ptr->string, cmd, sep);
    }

    // TODO: make this a utility function String -> cstring
    memcpy(buffer, file_path.str, file_path.size);
    buffer[file_path.size] = '\0';

    if (access(buffer, X_OK) == 0) {
      result = file_path;
      break;
    }
  }

  temp_arena_memory_end(temp);

  return result;
}

internal void type(Arena *a, ShellCommand *shell_cmd, StringList *builtin_cmds,
                   StringList *env_path_list) {
  assert(shell_cmd->args.node_count == 2);

  String exe = shell_cmd->args.first->next->string;
  if (is_builtin(exe, builtin_cmds)) {
    // TODO: improve printing for String
    str_print(exe);
    printf(" is a shell builtin\n");
  } else {
    String exe_path = search_path(a, exe, env_path_list);
    if (exe_path.size > 0) {
      printf("%.*s is %.*s\n", (int)exe.size, exe.str, (int)exe_path.size,
             exe_path.str);
    } else {
      printf("%.*s not found\n", (int)exe.size, exe.str);
    }
  }
}

internal void run_exec(Arena *a, ShellCommand *shell_cmd) {
  TempArenaMemory temp = temp_arena_memory_begin(a);

  char **args = (char **)arena_alloc(a, sizeof(char *) *
                                            (shell_cmd->args.node_count + 1));
  StringNode *ptr = shell_cmd->args.first;
  for (int index = 0; ptr != NULL; ptr = ptr->next, index += 1) {
    char *buf = (char *)arena_alloc(a, ptr->string.size + 1);
    memcpy(buf, ptr->string.str, ptr->string.size);
    buf[ptr->string.size] = '\0';
    args[index] = buf;
  }
  args[shell_cmd->args.node_count] = NULL;

  int pipe_stdout[2];
  int pipe_stderr[2];
  pipe(pipe_stdout);
  pipe(pipe_stderr);

  pid_t pid = fork();

  // child process
  if (pid == 0) {

    // close read end
    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    // redirect
    dup2(pipe_stdout[1], STDOUT_FILENO);
    dup2(pipe_stderr[1], STDERR_FILENO);
    // make sure only stdout/stderr points to the pipe
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    execvp(args[0], args);

  } else {
    // close write end
    close(pipe_stdout[1]);
    close(pipe_stderr[1]);

    char stdout_buf[128], stderr_buf[128];
    size_t stdout_n, stderr_n;

    while (true) {
      stdout_n = read(pipe_stdout[0], stdout_buf, sizeof(stdout_buf));
      stderr_n = read(pipe_stderr[0], stderr_buf, sizeof(stderr_buf));

      if (stdout_n > 0) {
        fwrite(stdout_buf, 1, stdout_n, stdout);
      }
      if (stderr_n > 0) {
        fwrite(stderr_buf, 1, stderr_n, stderr);
      }

      if (stdout_n <= 0 && stderr_n <= 0) {
        break;
      }
    }

    close(pipe_stdout[0]);
    close(pipe_stderr[0]);
    waitpid(pid, NULL, 0);
    temp_arena_memory_end(temp);
  }
}

internal void run(Arena *a, ShellCommand *shell_cmd, StringList *builtin_cmds,
                  StringList *env_path_list) {
  assert(shell_cmd->exe.size > 0);

  String exe = shell_cmd->exe;
  String exe_path = search_path(a, exe, env_path_list);
  if (exe_path.size > 0) {
    run_exec(a, shell_cmd);
  } else {
    printf("%.*s: command not found\n", (int)exe.size, exe.str);
  }
}

internal void pwd(Arena *a, ShellCommand *shell_cmd) {
  assert(shell_cmd->args.node_count == 1);

  char *buf = (char *)arena_alloc(a, PATH_MAX_LEN);
  getcwd(buf, PATH_MAX_LEN);
  printf("%s\n", buf);
}

internal bool is_directory(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) {
    return S_ISDIR(st.st_mode);
  }
  return false;
}

internal void cd(Arena *a, ShellCommand *shell_cmd) {
  assert(shell_cmd->args.node_count == 2);

  char *env_home = getenv("HOME");

  TempArenaMemory temp = temp_arena_memory_begin(a);
  String dir = shell_cmd->args.first->next->string;
  char *buf = (char *)arena_alloc(a, PATH_MAX_LEN);
  memcpy(buf, dir.str, dir.size);
  buf[dir.size] = '\0';

  if (str_equal_cstr(dir, "~")) {
    buf = env_home;
  }

  if (is_directory(buf)) {
    chdir(buf);
  } else {
    printf("cd: %s: No such file or directory\n", buf);
  }
  temp_arena_memory_end(temp);
}

internal RedirectInfo parse_redirect(String s, StringNode *file_name) {
  RedirectInfo info = {0};
  if (file_name == NULL) {
    return info;
  }

  int source_fd = -1;

  if (str_equal_cstr(s, ">") || str_equal_cstr(s, "1>") ||
      str_equal_cstr(s, "2>")) {
    info.source_fd = s.size == 1 ? 1 : s.str[0] - '0';
    info.output_file_name = file_name->string;
    info.flag = O_TRUNC;
  } else if (str_equal_cstr(s, ">>") || str_equal_cstr(s, "1>>") ||
             str_equal_cstr(s, "2>")) {
    info.source_fd = s.size == 2 ? 1 : s.str[0] - '0';
    info.output_file_name = file_name->string;
    info.flag = O_APPEND;
  }

  return info;
}

internal String eval_token(Arena *a, StringList *tokens) {
  assert(tokens != NULL);
  assert(tokens->node_count > 0);
  assert(tokens->first != NULL);
  assert(tokens->last != NULL);

  char *buf = (char *)arena_alloc(a, tokens->total_size);
  char *buf_ptr = buf;

  StringNode *ptr = tokens->first;
  for (; ptr != NULL; ptr = ptr->next) {
    char ch = ptr->string.str[0];
    if (ch == SINGLE_QUOTE) {
      // treated literally
      size_t cpy_size = ptr->string.size - 2;
      memcpy(buf_ptr, ptr->string.str + 1, cpy_size);
      buf_ptr += cpy_size;
    } else if (ch == DOUBLE_QUOTE) {
      String str = ptr->string;
      bool escape = false;

      for (int i = 1; i < str.size - 1; i += 1) {
        char ch = str.str[i];
        if (escape) {
          if (ch == DOUBLE_QUOTE || ch == BACKSLASH) {
            *(buf_ptr - 1) = ch;
          } else {
            *buf_ptr = ch;
            buf_ptr += 1;
          }
          escape = false;
        } else {
          *buf_ptr = ch;
          buf_ptr += 1;

          if (ch == BACKSLASH) {
            escape = true;
          }
        }
      }
    } else {
      // no quote
      String str = ptr->string;
      bool escape = false;

      for (int i = 0; i < str.size; i += 1) {
        char ch = str.str[i];
        if (escape) {
          *(buf_ptr - 1) = ch;
          escape = false;
        } else {
          *buf_ptr = ch;
          buf_ptr += 1;

          if (ch == BACKSLASH) {
            escape = true;
          }
        }
      }
    }
  }

  size_t size = buf_ptr - buf;
  String token = {.str = (uint8_t *)buf, .size = size};
  return token;
}

internal ShellCommand parse_command(Arena *a, char *cmd_str) {
  StringList tokens = {0};
  String cmd = {.str = (uint8_t *)cmd_str, .size = strlen(cmd_str)};

  int start = 0;
  for (; start <= cmd.size; start += 1) {
    // consume prefixing spaces
    for (;
         start < cmd.size && (cmd.str[start] == ' ' || cmd.str[start] == '\t');
         start += 1)
      ;

    int end = start;
    StringList tokens_with_quote = {0};
    char current_quote = '\0'; // default empty: no quote, can also be " or '
    char prev_ch = '\0';

    for (; end < cmd.size; end += 1) {
      char ch = cmd.str[end];

      if ((ch == SINGLE_QUOTE || ch == DOUBLE_QUOTE) && prev_ch != BACKSLASH) {
        if (ch == current_quote) {
          // quote finished
          str_list_push(a, &tokens_with_quote, str_substr(cmd, start, end + 1));
          current_quote = '\0';
          start = end + 1;
        } else if (current_quote == '\0') {
          // starting quote, the previous token should be pushed
          if (end > start) {
            str_list_push(a, &tokens_with_quote, str_substr(cmd, start, end));
          }
          current_quote = ch;
          start = end;
        }
      } else if ((ch == ' ' || ch == '\t') && current_quote == '\0' &&
                 prev_ch != BACKSLASH) {
        // not in a quote, and sees a space or tab
        str_list_push(a, &tokens_with_quote, str_substr(cmd, start, end));
        start = end;
        break;
      } else if (end + 1 == cmd.size) {
        str_list_push(a, &tokens_with_quote, str_substr(cmd, start, end + 1));
        start = end + 1;
        break;
      }

      prev_ch = ch;
    }

    if (tokens_with_quote.node_count > 0) {
      String token = eval_token(a, &tokens_with_quote);
      str_list_push(a, &tokens, token);
    }
  }

  StringList args = {0};
  StringNode *token_ptr = tokens.first;
  RedirectInfo redirect_info = {0};

  for (; token_ptr != NULL; token_ptr = token_ptr->next) {
    RedirectInfo info = parse_redirect(token_ptr->string, token_ptr->next);
    if (info.source_fd > 0) {
      redirect_info = info;
    } else {
      if (redirect_info.source_fd <= 0) {
        str_list_push(a, &args, token_ptr->string);
      }
    }
  }

  ShellCommand shell_cmd = {
      .exe = tokens.first->string,
      .args = args,
      .redir_info = redirect_info,
  };
  return shell_cmd;
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  uint8_t *arena_backing_buffer = (uint8_t *)malloc(4 * MB);
  Arena arena = {0};
  arena_init(&arena, arena_backing_buffer, 4 * MB);

  StringList builtin_cmds = {0};
  str_list_push_cstr(&arena, &builtin_cmds, "type");
  str_list_push_cstr(&arena, &builtin_cmds, "echo");
  str_list_push_cstr(&arena, &builtin_cmds, "exit");
  str_list_push_cstr(&arena, &builtin_cmds, "pwd");
  str_list_push_cstr(&arena, &builtin_cmds, "cd");

  char *env_path = getenv("PATH");
  StringList env_path_list = str_split_cstr(&arena, env_path, ":");

  while (true) {
    printf("$ ");

    char cmd[1024];
    fgets(cmd, sizeof(cmd), stdin);
    cmd[strcspn(cmd, "\n")] = '\0';

    TempArenaMemory temp = temp_arena_memory_begin(&arena);

    ShellCommand shell_cmd = parse_command(&arena, cmd);
    if (shell_cmd.exe.size == 0) {
      continue;
    }

    int saved_source_fd = 0;
    if (shell_cmd.redir_info.source_fd > 0) {
      char *file_name =
          to_cstring(&arena, shell_cmd.redir_info.output_file_name);
      int fd =
          open(file_name, O_WRONLY | O_CREAT | shell_cmd.redir_info.flag, 0644);

      saved_source_fd = dup(shell_cmd.redir_info.source_fd);
      dup2(fd, shell_cmd.redir_info.source_fd);
      close(fd);
    }

    if (str_equal_cstr(shell_cmd.exe, "exit")) {
      break;
    } else if (str_equal_cstr(shell_cmd.exe, "echo")) {
      echo(&shell_cmd);
    } else if (str_equal_cstr(shell_cmd.exe, "pwd")) {
      pwd(&arena, &shell_cmd);
    } else if (str_equal_cstr(shell_cmd.exe, "type")) {
      type(&arena, &shell_cmd, &builtin_cmds, &env_path_list);
    } else if (str_equal_cstr(shell_cmd.exe, "cd")) {
      cd(&arena, &shell_cmd);
    } else {
      run(&arena, &shell_cmd, &builtin_cmds, &env_path_list);
    }

    if (saved_source_fd > 0) {
      dup2(saved_source_fd, shell_cmd.redir_info.source_fd);
      close(saved_source_fd);
    }

    temp_arena_memory_end(temp);
  }

  free(arena_backing_buffer);
  return 0;
}
