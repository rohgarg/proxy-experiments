#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

extern int foo(); // A function in a library to be linked in.

extern void *segment_address[];

__attribute__((constructor))
void constructor() {
  printf("Constructor here.\n");
}

__attribute__((destructor))
void destructor() {
  printf("Destructor here.\n");
}

int main(int argc, char **argv, char **envp)
{
  segment_address[3] = sbrk(0);

  if (argc == 1) {// standalone if no pipefd
    fprintf(stderr,
            "_start, __data_start, _edata, END_OF_HEAP: %p, %p, %p, %p\n",
            segment_address[0], segment_address[1], segment_address[2], 
            segment_address[3]);
    static int dummy = 0;
    while (!dummy);
    foo();
    return 0;
  }

  return 0;
}
