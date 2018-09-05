// This could be libmpi.a or libproxy.a, with code to translate
//   between an MPI function and its address (similarly to dlsym()).

#include <stdio.h>
#include <string.h>
#include <mpi.h>
#include "libproxy.h"

int foo() {
  fprintf(stderr, "Foo is here (This is a fnc. in libproxy.a\n");
}

static void* MPI_Fnc_Ptrs[] = {
  NULL,
  FOREACH_FNC(GENERATE_FNC_PTR)
  NULL,
};

void *mydlsym(enum MPI_Fncs fnc)
{
  if (fnc < MPI_Fnc_NULL || fnc > MPI_Fnc_Invalid) {
    return NULL;
  }
  return MPI_Fnc_Ptrs[fnc];
}
