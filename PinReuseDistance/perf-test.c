#include <locale.h>
#include <stdlib.h>
#include "perfctr.h"

void do_work() {
  int i = 0;
  int *bigarray = malloc(10000000 * 16 * sizeof(int));
  for (i = 0; i < 10000000; i++) {
    //bigarray[rand() % (10000000 * 16)] = 1;
    bigarray[i * 16] = 1;
  }
}

int main(int argc, char **argv) {
  int* fds = perfctr_init();
  setlocale(LC_ALL, "");
  perfctr_start(fds, 2);

  do_work();

  perfctr_stop(fds, 2);
  perfctr_read(fds[0]);
  return 0;
}
