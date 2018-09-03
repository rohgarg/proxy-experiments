#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/stat.h>

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))

extern int foo(); // A function in a library to be linked in.

extern void _start();
extern void *__data_start;
extern void *_edata;

extern int main(int argc, char *argv[], char *envp[]);
extern int __libc_csu_init (int argc, char **argv, char **envp);
extern void __libc_csu_fini (void);

extern int __libc_start_main (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
                           int argc,
                           char **argv,
                           __typeof (main) init,
                           void (*fini) (void),
                           void (*rtld_fini) (void),
                           void *stack_end);

void *segment_address[] =
  {&_start, &__data_start, &_edata, NULL, &foo,
   NULL, &__libc_start_main, NULL, NULL, NULL};
  // If we didn't want to use sbrk(), then &_bss_start is also available.

static void
getTextSegmentRange(unsigned long *start,       // OUT
                    unsigned long *end,         // OUT
                    unsigned long *stackstart)  // OUT
{
  // From man 5 proc: See entry for /proc/[pid]/stat
  int pid;
  char cmd[PATH_MAX]; char state;
  int ppid; int pgrp; int session; int tty_nr; int tpgid;
  unsigned flags;
  unsigned long minflt; unsigned long cminflt; unsigned long majflt;
  unsigned long cmajflt; unsigned long utime; unsigned long stime;
  long cutime; long cstime; long priority; long nice;
  long num_threads; long itrealvalue;
  unsigned long long starttime;
  unsigned long vsize;
  long rss;
  unsigned long rsslim; unsigned long startcode; unsigned long endcode;
  unsigned long startstack; unsigned long kstkesp; unsigned long kstkeip;
  unsigned long signal_map; unsigned long blocked; unsigned long sigignore;
  unsigned long sigcatch; unsigned long wchan; unsigned long nswap;
  unsigned long cnswap;
  int exit_signal; int processor;
  unsigned rt_priority; unsigned policy;

  FILE* f = fopen("/proc/self/stat", "r");
  if (f) {
    fscanf(f, "%d "
              "%s %c "
              "%d %d %d %d %d "
              "%u "
              "%lu %lu %lu %lu %lu %lu "
              "%ld %ld %ld %ld %ld %ld "
              "%llu "
              "%lu "
              "%ld "
              "%lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu %lu "
              "%d %d %u %u",
           &pid,
           cmd, &state,
           &ppid, &pgrp, &session, &tty_nr, &tpgid,
           &flags,
           &minflt, &cminflt, &majflt, &cmajflt, &utime, &stime,
           &cutime, &cstime, &priority, &nice, &num_threads, &itrealvalue,
           &starttime,
           &vsize,
           &rss,
           &rsslim, &startcode, &endcode, &startstack, &kstkesp, &kstkeip,
           &signal_map, &blocked, &sigignore, &sigcatch, &wchan, &nswap,
           &cnswap,
           &exit_signal, &processor,
           &rt_priority, &policy);
  }
  fclose(f);
  *start      = startcode;
  *end        = endcode;
  *stackstart = startstack;
}

__attribute__((constructor))
void first_constructor()
{
  static int firstTime = 1;

  if (firstTime) {
    printf("First constructor here. We'll pass information to the parent.\n");
    firstTime = 0;
    // __libc_start_main (main=0x801c60 <main>, argc=1, argv=0x7fffffffd9d8, init=0x802a70 <__libc_csu_init>, fini=0x802b00 <__libc_csu_fini>, rtld_fini=0x0, stack_end=0x7fffffffd9c8);
    // _start();
    // The application calls __libc_start_main of the proxy. The proxy main calls setcontext to get back to the application.
    // unsigned long stackStart = getStackPtr();
    // NOTE: proc-stat returns the address of argc on the stack.
    // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
    // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
    // __libc_start_main(&main, *(int*)stackStart,
    //                   (char**)(stackStart + sizeof(unsigned long)),
    //                   &__libc_csu_init, &__libc_csu_fini, 0,
    //                   (void*)(stackStart - sizeof(unsigned long)));

    unsigned long start, end, stackstart;
    getTextSegmentRange(&start, &end, &stackstart);
    start = ROUND_UP(start);
    end   = ROUND_UP(end);

    segment_address[3] = sbrk(0);
    int argc = *(int*)stackstart;
    char **argv = (char**)(stackstart + sizeof(unsigned long));

    if (argc == 1) {// standalone if no pipefd
      return;
    }
    segment_address[5] = (void*)(end - start);
    segment_address[6] = &__libc_start_main;
    segment_address[7] = &main;
    segment_address[8] = &__libc_csu_init;
    segment_address[9] = &__libc_csu_fini;
    int pipe_write = atoi(argv[1]);
    int addr_size = sizeof segment_address;
    write(pipe_write, &addr_size, sizeof addr_size);
    write(pipe_write, segment_address, addr_size);
    close(pipe_write);
    // Allow some time for parent to copy bits of child before we exit.
    sleep(10);
    exit(0);
  } else {
    printf("Constructor called for the second time here. Running in the parent.\n");
  }
}

__attribute__((destructor))
void second_destructor()
{
  printf("Second destructor here. The application called exit in the destructor to get here. After this, we call setcontext() to get back in the application.\n");
}
