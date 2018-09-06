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
#include <syscall.h>
#include <sys/prctl.h>
#include <asm/prctl.h>
#include <link.h>
#include <sys/auxv.h>
#include <ucontext.h>
#include <mpi.h>
#include <string.h>
#include <sys/personality.h>

#include "libproxy.h"

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

typedef void* (*proxyDlsym_t)(enum MPI_Fncs fnc);

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

static unsigned long origPhnum;
static unsigned long origPhdr;

static void
patchAuxv (ElfW(auxv_t) *av, unsigned long phnum, unsigned long phdr, int save)
{
  for (; av->a_type != AT_NULL; ++av) {
    switch (av->a_type) {
      case AT_PHNUM:
        if (save) {
          origPhnum = av->a_un.a_val;
          av->a_un.a_val = phnum;
        } else {
          av->a_un.a_val = origPhnum;
        }
        break;
      case AT_PHDR:
        if (save) {
         origPhdr = av->a_un.a_val;
         av->a_un.a_val = phdr;
        } else {
          av->a_un.a_val = origPhdr;
        }
        break;
      default:
        break;
    }
  }
}

char **copyArgv(int argc, char **argv)
{
  char **new_argv = malloc((argc+1) * sizeof *new_argv);
  for(int i = 0; i < argc; ++i)
  {
      size_t length = strlen(argv[i])+1;
      new_argv[i] = malloc(length);
      memcpy(new_argv[i], argv[i], length);
  }
  new_argv[argc] = NULL;
  return new_argv;
}

void *segment_address[100];
  // Will be read from child (from original proxy)
  // Values are:  {&_start, &__data_start, &_edata, sbrk(0), ...}
  // Allocate extra space, for future growth

proxyDlsym_t pdlsym;

#define NEXT_FNC(func)                                                                \
  ({                                                                                  \
    static __typeof__(&MPI_##func)_real_MPI_## func = (__typeof__(&MPI_##func)) - 1;  \
    if (_real_MPI_ ## func == (__typeof__(&MPI_##func)) - 1) {                        \
      _real_MPI_ ## func = (__typeof__(&MPI_##func))pdlsym(MPI_Fnc_##func);           \
    }                                                                                 \
    _real_MPI_ ## func;                                                               \
  })

#define _real_MPI_Init       NEXT_FNC(Init)
#define _real_MPI_Finalize   NEXT_FNC(Finalize)
#define _real_MPI_Comm_size  NEXT_FNC(Comm_size)
#define _real_MPI_Comm_rank  NEXT_FNC(Comm_rank)

int main(int argc, char **argv, char **envp)
{
  int pipefd[2];

  if (strstr(argv[0], "copy-bits-norandom")) {
    char buf[strlen(argv[0])];
    memset(buf, 0, sizeof buf);
    readlink(argv[0], buf, sizeof buf);
    char **newArgv = copyArgv(argc, argv);
    newArgv[0] = buf;
    personality(ADDR_NO_RANDOMIZE);
    execvp(newArgv[0], newArgv);
  }

  pipe(pipefd);
  printf("Upper half's sbrk: %p\n", sbrk(0));
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
    execvp("./proxy-norandom", args);
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
  unsigned long proxy_fs = (unsigned long)segment_address[14];
  unsigned long app_fs;
  int ret = syscall(SYS_arch_prctl, ARCH_GET_FS, &app_fs);
  ret = syscall(SYS_arch_prctl, ARCH_SET_FS, proxy_fs);
  fprintf(stderr, "proxy FS: 0x%x; app FS: 0x%x; ret: %d; "
                  "Proxy PHNUM: %d; Proxy PHDR: 0x%x\n",
          proxy_fs, app_fs, ret, segment_address[15], segment_address[16]);
  if (ret < 0) {
    perror("ARCH_SET_FS");
  }
  void *stack_end = (void*)(stackStart - sizeof(unsigned long));
  char **ev = &argv[argc + 1];

  ElfW(auxv_t) *auxvec;
  {
    char **evp = ev;
    while (*evp++ != NULL);
    auxvec = (ElfW(auxv_t) *) evp;
  }
  patchAuxv(auxvec,
            (unsigned long)segment_address[15],
            (unsigned long)segment_address[16],
            1);
  int flag = 0;
  fprintf(stderr, "App: setting app context to %p\n", segment_address[17]);
  ret = getcontext((ucontext_t*)segment_address[17]);
  if (ret < 0) {
    perror("getcontext");
  }
  if (!flag) {
    flag = 1;
    fnc(segment_address[7], argc, argv,
        segment_address[8], segment_address[9], 0,
        stack_end);
  }
  fprintf(stderr, "After getcontext\n");
  ret = syscall(SYS_arch_prctl, ARCH_SET_FS, app_fs);
  patchAuxv(auxvec, 0, 0, 0);
  pdlsym = segment_address[18];
  ret = syscall(SYS_arch_prctl, ARCH_SET_FS, proxy_fs);
  ret = _real_MPI_Init(&argc, &argv);
  int world_size, world_rank;
  ret = _real_MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  ret = _real_MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  fprintf(stderr, "[%d/%d] Hello World!\n", world_rank, world_size);
  ret = _real_MPI_Finalize();
  ret = syscall(SYS_arch_prctl, ARCH_SET_FS, app_fs);
  // foo();

  return 0;
}

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))
#define ROUND_DOWN(addr) ((unsigned long)addr & ~(getpagesize()-1))
void read_proxy_bits(int childpid)
{
  struct iovec remote_iov[4];
  // text segment
  fprintf(stderr, "Segment-0 [%x:%x] from proxy\n", segment_address[0], (unsigned long)segment_address[0] + (unsigned long)segment_address[5]);
  remote_iov[0].iov_base = segment_address[0];
  remote_iov[0].iov_len = (size_t)segment_address[5];
  mmap_iov(&remote_iov[0], PROT_READ|PROT_EXEC|PROT_WRITE);
  // data segment
  fprintf(stderr, "Segment-1 [%x:%x] from proxy\n", segment_address[1], segment_address[3]);
  remote_iov[1].iov_base = segment_address[1];
  remote_iov[1].iov_len = segment_address[3] - segment_address[1];
  mmap_iov(&remote_iov[1], PROT_READ|PROT_WRITE);
  // RO data segment
  fprintf(stderr, "Segment-2 [%x:%x] from proxy\n", segment_address[10], segment_address[11]);
  remote_iov[2].iov_base = segment_address[10];
  remote_iov[2].iov_len = segment_address[11] - segment_address[10];
  mmap_iov(&remote_iov[2], PROT_READ | PROT_WRITE);
  // RW data segment
  fprintf(stderr, "Segment-3 [%x:%x] from proxy\n", segment_address[12], segment_address[13]);
  remote_iov[3].iov_base = segment_address[12];
  remote_iov[3].iov_len = segment_address[13] - segment_address[12];
  mmap_iov(&remote_iov[3], PROT_READ | PROT_WRITE);
  // NOTE:  In our case loca_iov will be same as remote_iov.
  // NOTE:  This requires same privilege as ptrace_attach (valid for child)
  for (int i = 0; i < 4; i++) {
    fprintf(stderr, "Reading segment-%d [%x:%x] from proxy\n", i, remote_iov[i].iov_base, remote_iov[i].iov_len);
    int rc = process_vm_readv(childpid, remote_iov + i, 1, remote_iov + i, 1, 0);
    if (rc == -1) {
      perror("process_vm_readv"); exit(1);
    }
  // rc = process_vm_readv(childpid, remote_iov+1, 1, remote_iov+1, 1, 0);
  // if (rc == -1) {
  //   perror("process_vm_readv"); exit(1);
  // }
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
