#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>
#include <ucontext.h>

extern int foo(); // A function in a library to be linked in.

extern void *segment_address[];

extern ucontext_t appContext;

int main(int argc, char **argv, char **envp)
{
  if (argc == 1) {// standalone if no pipefd
    fprintf(stderr,
            "PROXY main: _start, __data_start, _edata, END_OF_HEAP: %p, %p, %p, %p\n",
            segment_address[0], segment_address[1], segment_address[2], 
            segment_address[3]);
    int ret = setcontext(&appContext);
    if (ret < 0) {
      perror("PROXY main: setcontext");
    }
    return 0;
  }

  return 0;
}
