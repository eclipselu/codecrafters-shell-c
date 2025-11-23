#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char *argv[]) {
  // Flush after every printf
  setbuf(stdout, NULL);

  // TODO: Uncomment the code below to pass the first stage
  printf("$ ");

  char cmd[1024];
  fgets(cmd, sizeof(cmd), stdin);
  cmd[strcspn(cmd, "\n")] = '\0';

  printf("%s: command not found\n", cmd);

  return 0;
}
