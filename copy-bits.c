// Needed for process_vm_readv
#define _GNU_SOURCE
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/uio.h>
#include <sys/mman.h>

void read_proxy_bits(int childpid);
void mmap_iov(const struct iovec *iov, int prot);

void *segment_address[100];
  // Will be read from child (from original proxy)
  // Values are:  {&_start, &__data_start, &_edata, sbrk(0), ...}
  // Allocate extra space, for future growth


int main() {
  int pipefd[2];

  pipe(pipefd);
  int childpid = fork();

  if (childpid > 0) { // if parent
    close(pipefd[1]); // we won't be needing write end of pipe
    int addr_size;
    read(pipefd[0], &addr_size, sizeof addr_size);
    int rc = read(pipefd[0], segment_address, addr_size);
    fprintf(stderr,
        "_start, __data_start, _edata, END_OF_HEAP: 0x%x, 0x%x, 0x%x, 0x%x\n",
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

  void (*foo)() = segment_address[4];
  foo();

  return 0;
}

#define ROUND_UP(addr) ((addr + getpagesize() - 1) & ~(getpagesize()-1))
#define ROUND_DOWN(addr) ((unsigned long)addr & ~(getpagesize()-1))
void read_proxy_bits(int childpid) {
  struct iovec remote_iov[2];
  // text segment
  remote_iov[0].iov_base = segment_address[0];
  // FIXME:  For now, use this current size of text.  Must be fiex.
  remote_iov[0].iov_len = 0x8bd000-0x800000;
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
