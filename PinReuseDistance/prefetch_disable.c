/*
 * prefetch.c
 *
 *  Created on: Sep 21, 2010
 *      Author: dschuff
 *      Derived from {disable,enable}_core2_prefetch by Vince Weaver
 */

#include <err.h>
#include <errno.h>
#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <getopt.h>
#include <inttypes.h>
#include <sys/types.h>

#define MAX_CPUS 8

uint32_t core2_reg = 0x1a0;
uint32_t barcelona_reg = 0xc0011022;
uint32_t nehalem_reg = 0x1a4;

uint32_t msr_reg;
uint64_t (*modify_msr) (uint64_t, bool, bool) = NULL;
void (*print_msr) (uint64_t) = NULL;

void print_msr_core2(uint64_t data) {
  /*printf("        Fast strings         = %d\n", !!(data & 0x1));
    printf("        Thermal control      = %d\n", !!(data & (1ULL << 3)));
    printf("        Performance mon      = %d\n", !!(data & (1ULL << 7)));*/
    printf("        HW prefetch disabled = %d\n", !!(data & (1ULL << 9)));
    /*    printf("        FERR# multiplex      = %d\n", !!(data & (1ULL << 10)));
    printf("        Branch trace unavail = %d\n", !!(data & (1ULL << 11)));
    printf("        PEBS unavail         = %d\n", !!(data & (1ULL << 12)));
    printf("        Therm avail          = %d\n", !!(data & (1ULL << 13)));
    printf("        Speedstep            = %d\n", !!(data & (1ULL << 16)));
    printf("        FSM                  = %d\n", !!(data & (1ULL << 18)));*/
    printf("        Adjacent Cache Disab = %d\n", !!(data & (1ULL << 19)));
    /*    printf("        Speedstep lock       = %d\n", !!(data & (1ULL << 20)));
    printf("        Limit CPU Maxval     = %d\n", !!(data & (1ULL << 22)));
    printf("        xTPR disable         = %d\n", !!(data & (1ULL << 23)));*/
    printf("        L1 context mode      = %d\n", !!(data & (1ULL << 24)));
    //printf("        XD disable           = %d\n", !!(data & (1ULL << 34)));
    printf("        DCU prefetch disable = %d\n", !!(data & (1ULL << 37)));
    //    printf("        IDA accel disable    = %d\n", !!(data & (1ULL << 38)));
    printf("        IP prefetch disable  = %d\n", !!(data & (1ULL << 39)));
}

uint64_t modify_msr_core2(uint64_t data, bool enable, bool print) {
  if (print) {
    char *ena = enable ? "ENABLING" : "DISABLING";
    printf(" %s prefetching using map for Intel Core 2. Current value:\n", ena);
    print_msr_core2(data);
  }

  if (enable) {
    data&= ~((1ULL<<9)|(1ULL<<19)|(1ULL<<37)|(1ULL<<39));
  } else {
    //data |= (1ULL << 9) | (1ULL << 19) | (1ULL << 37) | (1ULL << 39);//disable all
    //    data |= (1ULL << 37);//disable DCU
    //    data |= (1ULL << 39);//disable IP
    //    data |= (1ULL << 37) | (1ULL << 39); //disable DCU,IP (into L1)
        data |= (1ULL << 9) | (1ULL << 19); //disable HW,ACL (into L2)
    //    data |= (1ULL << 9) | (1ULL << 19) | (1ULL << 37); //disable all but IP
    //    data |= (1ULL << 9) | (1ULL << 19) | (1ULL << 39); //disable all but DCU
  }
  return data;
}

void print_msr_barcelona(uint64_t data) {
  printf("  Disable spec TLB reloads  = %d\n", !!(data & (1ULL << 4)));
  printf("  Disable SMC checking      = %d\n", !!(data & (1ULL << 8)));
  printf("  Disable HW prefetching    = %d\n", !!(data & (1ULL << 13)));
  printf("  prefetch request counter  = %d\n", !!(data & (3ULL << 34)));
}

uint64_t modify_msr_barcelona(uint64_t data, bool enable, bool print) {
  if (print) {
    char *ena = enable ? "ENABLING" : "DISABLING";
    printf(" %s prefetching using map for AMD Opteron family 16. Current value:\n", ena);
    print_msr_barcelona(data);
  }

  if (enable) {
    data &= ~(1ULL << 13);
  } else {
    data |= (1ULL << 13);
  }
  return data;
}

void print_msr_nehalem(uint64_t data) {
  printf("  MLC Streamer prefetcher  = %d\n", !!(data & (1ULL))); 
  printf("  MLC Spatial prefetcher   = %d\n", !!(data & (1ULL << 1)));
  printf("  DCU Streamer prefetcher  = %d\n", !!(data & (1ULL << 2)));
  printf("  DCU IP prefetcher        = %d\n", !!(data & (1ULL << 3)));
}

uint64_t modify_msr_nehalem(uint64_t data, bool enable, bool print) {
  if (print) {
    char *ena = enable ? "ENABLING" : "DISABLING";
    printf(" %s prefetching using map for Intel Core i7. Current value:\n", ena);
    print_msr_nehalem(data);
  }
  if (enable) {
    data &= ~((1ULL) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3));
  } else {
    data |= (1ULL) | (1ULL << 1) | (1ULL << 2) | (1ULL << 3);
    //data |= (1ULL) | (1ULL << 1); // disable L2/MLC prefetching
    //data |= (1ULL << 2) | (1ULL << 3); //disable L1/DCU prefetching
  }
  return data;
}

void set_cpu_type() {
  int ret;
  char vendor[32];
  int family = 0, model = 0;
  FILE *cpuid = fopen("/proc/cpuinfo", "r");
  if (NULL == cpuid) err(1, "could not open /proc/cpuinfo");
  ret = fscanf(cpuid, "processor : 0 vendor_id : %s cpu family : %d model : %d",
      vendor, &family, &model);
  if (ret != 3) printf("fscanf returned %d\n", ret);
  if (!strcmp(vendor, "AuthenticAMD") && family == 16) {
    msr_reg = barcelona_reg;
    modify_msr = modify_msr_barcelona;
    print_msr = print_msr_barcelona;
  } else if (!strcmp(vendor, "GenuineIntel") && (model == 15 || family == 15)) {
    msr_reg = core2_reg;
    modify_msr = modify_msr_core2;
    print_msr = print_msr_core2;
  } else if (!strcmp(vendor, "GenuineIntel") && model == 30) {
    msr_reg = nehalem_reg;
    modify_msr = modify_msr_nehalem;
    print_msr = print_msr_nehalem;
  } else {
    errx(1, "Could not determine CPU type: cpuid vendor %s, family %d, model %d",
        vendor, family, model);
  }
}

int read_one_msr(int cpu, uint32_t reg, uint64_t *return_data) {
  uint64_t data;
  char msr_file_name[64];
  int fd;

  sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
  fd = open(msr_file_name, O_RDONLY);
  if (fd < 0) {
    if (errno == ENXIO || errno == ENOENT) {
      //fprintf(stderr, "rdmsr: No CPU %d\n", cpu);
      return 1;
    } else if (errno == EIO) {
      fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", cpu);
      exit(3);
    } else {
      warn("rdmsr: cpu %d open", cpu);
      printf("attempting to load msr kernel module...");
      int ret = system("modprobe msr");
      printf("%s\n", ret == 0 ? "success!" : "failed!");
      exit(127);
    }
  }
  
  if (pread(fd, &data, sizeof data, reg) != sizeof data) {
    if (errno == EIO) {
      fprintf(stderr, "rdmsr: CPU %d cannot read MSR 0x%08"PRIx32"\n", cpu,
              reg);
      exit(4);
    } else {
      perror("rdmsr: pread");
      exit(127);
    }
  }

  close(fd);
  *return_data = data;
  return 0;
}

void write_one_msr(int cpu, uint32_t reg, uint64_t data) {
  char msr_file_name[64];
  int fd;
  sprintf(msr_file_name, "/dev/cpu/%d/msr", cpu);
  fd = open(msr_file_name, O_WRONLY);
  if (fd < 0) {
    if (errno == ENXIO) {
      fprintf(stderr, "wrmsr: No CPU %d\n", cpu);
      exit(2);
    } else if (errno == EIO) {
      fprintf(stderr, "wrmsr: CPU %d doesn't support MSRs\n", cpu);
      exit(3);
    } else {
      perror("wrmsr: open");
      exit(127);
    }
  }

  if (pwrite(fd, &data, sizeof data, reg) != sizeof data) {
    if (errno == EIO) {
      fprintf(stderr, "wrmsr: CPU %d cannot set MSR "
              "0x%08"PRIx32" to 0x%016"PRIx64"\n", cpu, reg, data);
      exit(4);
    } else {
      perror("wrmsr: pwrite");
      exit(127);
    }
  }
  close(fd);
}


int write_msrs(int enable) {
  uint32_t reg;
  uint64_t data, readback_data;
  uint64_t core0_data;
  int cpu = 0;
  int print = false;

  reg = msr_reg;

  for (cpu = 0; cpu < MAX_CPUS; cpu++) {

    if(read_one_msr(cpu, reg, &data) == 1) continue;

    print = false;
    if (cpu == 0) {
      core0_data = data;
      print = true;
    } else if (data != core0_data) {
      printf("CPU %d MSR different from CPU 0\n", cpu);
      print = false;
    }

    if (print) printf("CPU %d: Current value is 0x%llx\n", cpu, (long long) data);

    data = modify_msr(data, enable, print);
    if (print) {
      printf("        Writing out new value: 0x%llx\n", (long long) data);
      print_msr(data);
    }

    write_one_msr(cpu, reg, data);

    read_one_msr(cpu, reg, &readback_data);
    if (data != readback_data) {
      printf("Read back data does not match written data:\n");
      print_msr(data);
    }

  }

  return 0;
}

int main(int argc, char *argv[]) {
  if (argc < 2 || (strncmp(argv[1], "e",1) && strncmp(argv[1], "d", 1) 
                   && strncmp(argv[1], "r", 1))) {
    printf("usage: %s <arg>, where arg begins with d (disable) or e (enable)\n",
        argv[0]);
    exit(1);
  }
  set_cpu_type();
  if(!strncmp(argv[1], "r", 1)) {
    uint64_t data;
    read_one_msr(0, msr_reg, &data);
    printf("Current value: 0x%llx\n", (long long)data);
    print_msr(data);
  } else {
    write_msrs(strncmp(argv[1], "d", 1));
  }
  return 0;
}
