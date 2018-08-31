#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

extern int foo(); // A function in a library to be linked in.

extern void _start();
extern void *__data_start;
extern void *_edata;

// extern void _start();

void *segment_address[] =
  {&_start, &__data_start, &_edata, NULL, &foo, (void *)(0x8bd000-0x800000)};
  // If we didn't want to use sbrk(), then &_bss_start is also available.
  // FIXME:  0x8bd000-0x800000 is current text segment size

__attribute__((constructor))
void constructor() {
  printf("Constructor here.\n");
}

// void entry()
// {
//   // atexit(&second_destructor);
//   // printf("In entry\n");
//   _start();
// }

__attribute__((destructor))
void destructor() {
  printf("Destructor here.\n");
}

int main(int argc, char *argv[]) 
{
  segment_address[3] = sbrk(0);

  if (argc == 1) {// standalone if no pipefd
    fprintf(stderr,
            "_start, __data_start, _edata, END_OF_HEAP: 0x%x, 0x%x, 0x%x, 0x%x\n",
            segment_address[0], segment_address[1], segment_address[2], 
            segment_address[3]);
    foo();
    return 0;
  }

  int pipe_write = atoi(argv[1]);
  int addr_size = sizeof segment_address;
  write(pipe_write, &addr_size, sizeof addr_size);
  write(pipe_write, segment_address, addr_size);
  close(pipe_write);

  // FIXME:  Note that if libmpi.a is present, we want to run
  //   after the constructor for libc.a, but before the constructor
  //   for libmpi.a.  We could add our own constructor in a library
  //   that runs before libmpi.a.  Also, we can directly call:
  //     __libc_start_main() if we wish to initialize libc ourselves.
  //   In this case, we would change the entry point for the proxy
  //   program to point to our own routine, which would call
  //     __libc_start_main and then continue with the above code.

  // Allow some time for parent to copy bits of child before we exit.
  sleep(50);

  return 0;
}
