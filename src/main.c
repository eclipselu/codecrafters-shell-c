#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  while (true) {
    printf("$ ");

    char cmd[1024];
    fgets(cmd, sizeof(cmd), stdin);
    cmd[strcspn(cmd, "\n")] = '\0';

    if (strcmp(cmd, "exit") == 0) {
      break;
    } else {
      printf("%s: command not found\n", cmd);
    }
  }

  return 0;
}
