#include <stdio.h>
#include "chplmem.h"
#include "chplmem_tests.h"
#include "chplrt.h"
#include "chpltypes.h"


void _chpl_memtest_printMemTable(int threshold) {
  printMemTable(threshold);
}


void _chpl_memtest_printMemStat(void) {
  printMemStat();
}


void _chpl_memtest_resetMemStat(void) {
  resetMemStat();
}


void _chpl_memtest_allocAndFree(void) {
  _int64* i;
  _bool* b;
  _real64* f;
  _complex64* c;

  resetMemStat();

  i = (_int64*) _chpl_malloc(1, sizeof(_int64), "_int64");
  fprintf(stdout, "malloc'd an int\n");
  printMemStat();
  b = (_bool*) _chpl_malloc(1, sizeof(_bool), "_bool");
  fprintf(stdout, "malloc'd abool\n");
  printMemStat();

  f = (_real64*) _chpl_calloc(1, sizeof(_real64), "_real64");
  *f = 99.9;
  fprintf(stdout, "calloc'd a real\n");
  printMemStat();
  c = (_complex64*) _chpl_calloc(1, sizeof(_complex64), "_complex64");
  c->re = 1.2;
  c->im = 2.3;
  fprintf(stdout, "calloc'd a complex\n");
  printMemStat();

  _chpl_free(i);
  _chpl_free(b);
  _chpl_free(c);
  fprintf(stdout, "freed the int, the bool, and the complex\n");
  printMemStat();

  f = _chpl_realloc(f, 10, sizeof(_real64), "_real64");
  fprintf(stdout, "realloc'd 10 times the real\n");
  printMemStat();
  
  _chpl_free(f);
  fprintf(stdout, "freed the real\n");
  printMemStat();
}


void _chpl_memtest_freedMalloc(void) {
  _int64* freedInt = (_int64*) _chpl_malloc(1, sizeof(_int64), "_int64");
  _real64* unfreedFloat = (_real64*) _chpl_malloc(1, sizeof(_real64), "_real64");
  *unfreedFloat = 99.9;
  
  printMemTable(0);
  _chpl_free(freedInt);
  fprintf(stdout, "And this is the memory hash table after the int "
          "variable has been freed:\n");
  printMemTable(0);
}


void _chpl_memtest_freedWithoutMalloc(void) {
  _int64 cantFreeMe = 13;
  _chpl_free(&cantFreeMe);
}


void _chpl_memtest_reallocWithoutMalloc(void) {
  _int64 cantReallocMe = 13;
  _chpl_realloc(&cantReallocMe, 3, sizeof(_int64*), "_int64");
}


void _chpl_memtest_reallocZeroSize(void) {
  _int64* makeMeZero = (_int64*) _chpl_malloc(1, sizeof(_int64), "_int64");
  printMemTable(0);
  _chpl_realloc(makeMeZero, 0, sizeof(_int64*), "_int64");
  fprintf(stdout, "After a call to realloc with size equal to 0:\n");
  printMemTable(0);
}


void _chpl_memtest_mallocOutOfMemory(void) {
  while (1) {
    int i = 100000;
    _chpl_malloc(i, sizeof(_int64), "malloc test");
  }
}


/* This test caused the "impossible" to happen when run with valgrind 2.1.2, 
   and ultimately crashed my desktop.  A bug report was submitted.  */ 
void _chpl_memtest_reallocOutOfMemory(void) {
  int i = 100000;
  _int64* biggerAndBigger = NULL;
  
  while (1) {
    biggerAndBigger = (_int64*) 
      _chpl_realloc(biggerAndBigger, i, sizeof(_int64), "_int64");
    i = i + 100000;
  }
}
