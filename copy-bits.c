// Needed for process_vm_readv
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/uio.h>
#include <sys/mman.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

void read_proxy_bits(int childpid);
void mmap_iov(const struct iovec *iov, int prot);

#ifdef MAIN_AUXVEC_ARG
/* main gets passed a pointer to the auxiliary.  */
# define MAIN_AUXVEC_DECL	, void *
# define MAIN_AUXVEC_PARAM	, auxvec
#else
# define MAIN_AUXVEC_DECL
# define MAIN_AUXVEC_PARAM
#endif

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

typedef int (*libcFptr_t) (int (*main) (int, char **, char ** MAIN_AUXVEC_DECL),
			    int ,
			    char **,
			    __typeof (main) ,
			    void (*fini) (void),
			    void (*rtld_fini) (void),
			    void *);

static unsigned long
getStackPtr()
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
  return startstack;
}

void *segment_address[100];
  // Will be read from child (from original proxy)
  // Values are:  {&_start, &__data_start, &_edata, sbrk(0), ...}
  // Allocate extra space, for future growth


int main(int argc, char **argv, char **envp)
{
  int pipefd[2];

  pipe(pipefd);
  int childpid = fork();

  if (childpid > 0) { // if parent
    close(pipefd[1]); // we won't be needing write end of pipe
    int addr_size;
    read(pipefd[0], &addr_size, sizeof addr_size);
    int rc = read(pipefd[0], segment_address, addr_size);
    fprintf(stderr,
        "_start, __data_start, _edata, END_OF_HEAP: %p, %p, %p, %p\n",
        segment_address[0], segment_address[1], segment_address[2], 
        segment_address[3]);
  } else if (childpid == 0) { // else if child
    close(pipefd[0]); // close reading end of pipe
    char *args[3] = {"proxy", "<REPLACE BY pipefd[1]>", NULL};
    char buf[10];
    snprintf(buf, sizeof buf, "%d", pipefd[1]); // write end of pipe
    args[1] = buf;
    execvp("./proxy", args);
    perror("execvp"); // shouldn't reach here
    exit(1);
  } else {
    assert(childpid == -1);
    perror("fork");
    exit(1);
  }

  read_proxy_bits(childpid);
  waitpid(childpid, NULL, 0);

  unsigned long stackStart = getStackPtr();
    // NOTE: proc-stat returns the address of argc on the stack.
    // argv[0] is 1 LP_SIZE ahead of argc, i.e., startStack + sizeof(void*)
    // Stack End is 1 LP_SIZE behind argc, i.e., startStack - sizeof(void*)
  // void (*foo)() = segment_address[4];
  libcFptr_t fnc = segment_address[6];
  fnc(segment_address[7], *(int*)stackStart,
      (char**)(stackStart + sizeof(unsigned long)),
      segment_address[8], segment_address[9], 0,
      (void*)(stackStart - sizeof(unsigned long)));
  // foo();

  return 0;
}

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))
#define ROUND_DOWN(addr) ((unsigned long)addr & ~(getpagesize()-1))
void read_proxy_bits(int childpid) {
  struct iovec remote_iov[2];
  // text segment
  remote_iov[0].iov_base = segment_address[0];
  // FIXME:  For now, use this current size of text.  Must be fiex.
  remote_iov[0].iov_len = (size_t)segment_address[5];
  mmap_iov(&remote_iov[0], PROT_READ|PROT_EXEC|PROT_WRITE);
  // data segment
  remote_iov[1].iov_base = segment_address[1];
  remote_iov[1].iov_len = segment_address[3] - segment_address[1];
  mmap_iov(&remote_iov[1], PROT_READ|PROT_WRITE);
  // NOTE:  In our case loca_iov will be same as remote_iov.
  // NOTE:  This requires same privilege as ptrace_attach (valid for child)
  int rc = process_vm_readv(childpid, remote_iov, 1, remote_iov, 1, 0);
  if (rc == -1) {
    perror("process_vm_readv"); exit(1);
  }
  rc = process_vm_readv(childpid, remote_iov+1, 1, remote_iov+1, 1, 0);
  if (rc == -1) {
    perror("process_vm_readv"); exit(1);
  }

  // Can remove PROT_WRITE now that we've oppulated the text segment.
  mprotect((void *)ROUND_DOWN(remote_iov[0].iov_base),
           ROUND_UP(remote_iov[0].iov_len),
           PROT_READ|PROT_EXEC);
}

void mmap_iov(const struct iovec *iov, int prot) {
  void *base = (void *)ROUND_DOWN(iov->iov_base);
  size_t len = ROUND_UP(iov->iov_len);
  void * rc = mmap(base, len, prot, MAP_PRIVATE|MAP_FIXED|MAP_ANONYMOUS, -1, 0);
  if (rc == MAP_FAILED) {
    perror("mmap"); exit(1);
  }
}
