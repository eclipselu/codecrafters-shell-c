#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <readline/history.h>
#include <readline/readline.h>
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

#include "arena.h"
#include "base.h"
#include "base_string.h"

// include builtin and executables in PATH
global StringList existing_commands = {0};
global const char *builtin_commands[] = {"type", "echo",    "exit", "pwd",
                                         "cd",   "history", NULL};

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

internal bool is_builtin(String cmd) {
  bool result = false;
  for (int i = 0; builtin_commands[i] != NULL; i += 1) {
    if (str_equal_cstr(cmd, builtin_commands[i])) {
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

internal void type(Arena *a, ShellCommand *shell_cmd,
                   StringList *env_path_list) {
  assert(shell_cmd->args.node_count == 2);

  String exe = shell_cmd->args.first->next->string;
  if (is_builtin(exe)) {
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

internal void run(Arena *a, ShellCommand *shell_cmd,
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
             str_equal_cstr(s, "2>>")) {
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

  String exe = tokens.first == NULL ? (String){0} : tokens.first->string;
  ShellCommand shell_cmd = {
      .exe = exe,
      .args = args,
      .redir_info = redirect_info,
  };
  return shell_cmd;
}

internal void preload_existing_commands(Arena *a, StringList *env_path_list) {
  assert(env_path_list != NULL);

  // existing commands
  int index = 0;
  while (builtin_commands[index] != NULL) {
    str_list_push_cstr(a, &existing_commands, builtin_commands[index]);
    index += 1;
  }

  for (StringNode *ptr = env_path_list->first; ptr != NULL; ptr = ptr->next) {
    char *dirpath = to_cstring(a, ptr->string);
    DIR *dir = opendir(dirpath);
    if (dir == NULL) {
      continue;
    }

    struct dirent *de = NULL;
    while ((de = readdir(dir)) != NULL) {
      char fullpath[PATH_MAX_LEN];
      snprintf(fullpath, sizeof(fullpath), "%s/%s", dirpath, de->d_name);

      struct stat st;
      if (stat(fullpath, &st) != 0) {
        continue;
      }

      if (S_ISREG(st.st_mode) && access(fullpath, X_OK) == 0) {
        str_list_push_cstr(a, &existing_commands, de->d_name);
      }
    }
  }
}

// TODO:: optimize the speed
internal char *cmd_generator(const char *text, int state) {
  local_persist StringNode *cmd_ptr = NULL;

  if (!state) {
    cmd_ptr = existing_commands.first;
  }

  while (cmd_ptr != NULL) {
    const String cmd = cmd_ptr->string;
    cmd_ptr = cmd_ptr->next;
    if (str_starts_with_cstr(cmd, text)) {
      return strndup((const char *)cmd.str, cmd.size);
    }
  }

  return NULL; // No more matches
}

internal char **cmd_completion(const char *text, int start, int end) {
  if (start == 0) {
    return rl_completion_matches(text, cmd_generator);
  }
  return NULL;
}

internal void print_history(Arena *a, int n) {
  for (int i = history_length - n; i < history_length; i += 1) {
    HIST_ENTRY *e = history_get(i + history_base);
    printf("    %d  %s\n", i + history_base, e->line);
  }
}

internal void history(Arena *a, ShellCommand *shell_cmd) {
  int argc = shell_cmd->args.node_count;
  assert(argc > 0);

  if (argc == 1) {
    print_history(a, history_length);
  } else if (argc == 2) {
    if (str_is_posnum(shell_cmd->args.last->string)) {
      int n = history_length;
      String s = shell_cmd->args.last->string;
      n = atoi(to_cstring(a, s));
      if (n > history_length) {
        n = history_length;
      }
      print_history(a, n);
    }
  } else if (argc == 3) {
    String flag = shell_cmd->args.first->next->string;
    String histfile = shell_cmd->args.last->string;
    if (str_equal_cstr(flag, "-r")) {
      read_history(to_cstring(a, histfile));
    } else if (str_equal_cstr(flag, "-w")) {
      write_history(to_cstring(a, histfile));
    }
  }
}

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  uint8_t *arena_backing_buffer = (uint8_t *)malloc(4 * MB);
  Arena arena = {0};
  arena_init(&arena, arena_backing_buffer, 4 * MB);

  char *env_path = getenv("PATH");
  StringList env_path_list = str_split_cstr(&arena, env_path, ":");
  char *env_histfile = getenv("HISTFILE");

  // setup readline
  // 1. completion
  rl_attempted_completion_function = cmd_completion;
  // 2. history
  using_history();
  if (env_histfile != NULL) {
    read_history(env_histfile);
  }

  while (true) {
    TempArenaMemory temp = temp_arena_memory_begin(&arena);

    preload_existing_commands(&arena, &env_path_list);

    char *cmd = NULL;
    cmd = readline("$ ");
    if (cmd == NULL) {
      continue;
    }
    add_history(cmd);

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
      type(&arena, &shell_cmd, &env_path_list);
    } else if (str_equal_cstr(shell_cmd.exe, "cd")) {
      cd(&arena, &shell_cmd);
    } else if (str_equal_cstr(shell_cmd.exe, "history")) {
      history(&arena, &shell_cmd);
    } else {
      run(&arena, &shell_cmd, &env_path_list);
    }

    if (saved_source_fd > 0) {
      dup2(saved_source_fd, shell_cmd.redir_info.source_fd);
      close(saved_source_fd);
    }

    free(cmd);

    temp_arena_memory_end(temp);
  }

  free(arena_backing_buffer);
  return 0;
}
