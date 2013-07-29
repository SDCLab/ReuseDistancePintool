/*
 * perfctr.c
 *
 *  Created on: Apr 13, 2010
 *      Author: dschuff
 */

#include "perfctr.h"
#include <err.h>
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <perfmon/pfmlib_perf_event.h>

static char *cache_events_core2_repl[]={
  "L1D_ALL_CACHE_REF",
  "L1D_REPL",
  NULL
};

static char *cache_events_core2_l2[]={
  "L1D_ALL_CACHE_REF",
  "L2_LINES_IN:BOTH_CORES",
  NULL
};

static char *cache_events_amd[]={
  "DATA_CACHE_ACCESSES",
  "DATA_CACHE_MISSES",
  //"DATA_PREFETCHES:ALL",// ? always 0?
  "REQUESTS_TO_L2:HW_PREFETCH_FROM_DC",
  "DATA_CACHE_REFILLS:ALL",
  NULL
};

static char *cache_events_amd_l2[]={
  //"REQUESTS_TO_L2:DATA",
  "DATA_CACHE_ACCESSES",
  "INSTRUCTION_CACHE_FETCHES",
  "L2_CACHE_MISS:DATA:INSTRUCTIONS",
  "L2_CACHE_MISS:ALL",
  NULL
};

static char *ipc_events[]={
  "PERF_COUNT_HW_CPU_CYCLES",
  "PERF_COUNT_HW_INSTRUCTIONS",
  NULL
};

static char *cache_events_core2_pref[]={
  "L1D_ALL_CACHE_REF",
  //  "L1D_ALL_REF",
  //  "L1D_REPL",
  "L1D_PREFETCH:REQUESTS",
  NULL
};

static char *cache_events_nehalem_test[]={
  "L1D_ALL_REF:CACHEABLE", //(0/1 only)
  "L2_DATA_RQSTS:PREFETCH_MESI",
  "L1D_PREFETCH:REQUESTS",
  "L1D:REPL", //(0/1 only)
  //  "L2_TRANSACTIONS:PREFETCH",
  NULL,
};

static char **g_events = cache_events_nehalem_test;
static int g_num_events = 4;
//static char **g_events2 = ipc_events;
static int g_event_sample_count = 1000000;
const static int kDefaultSamples;

static __thread int *perf_event_fds = NULL;
static __thread int *perf_event_fds2 = NULL;
static __thread int finalized = 0;

/* The initialized variable is only intended for situations when the first 
   thread to run runs a library constructor that calls hook_thread_start
   before the constructor from this library runs. In particular, perfctr_init is
   NOT thread-safe with respect to hook_thread_start, so the user must still
   ensure that either perfctr_init or the first hook_thread_start finishes before any
   other threads are spawned. Once perfctr_init is finished, additional
   calls to hook_thread_start are thread-safe with respect to each other.
 */
static int initialized = 0;
static int opinitialized = 0;
static int active_threads = 0;
static pthread_mutex_t count_lock;
static uint64_t g_event_counts[8];

static __thread double time_begin = 0;
static __thread double time_end = 0;

void __attribute__((constructor)) library_init() {
  perfctr_init();
}
void __attribute__ ((destructor)) library_fini() {
  perfctr_print(g_event_counts, g_events);
  //  perfctr_print(g_event_counts+g_num_events, g_events2);
}

static void get_events() {
  int i = 0;
  char **events = calloc(sizeof(char *), 8);
  char *env = getenv("EVENTS");

  if (!env) {
    warnx("Cannot find EVENTS env var, using default events");
    g_num_events = 2;
    g_events = ipc_events;
    return;
  }
  while ((events[i] = strtok(env, ",")) != NULL) {
    i++;
    env = NULL;
  }
  g_num_events = i;
  g_events = events;

  env = getenv("EVENT_COUNT");
  if (!env) {
    warnx("Using default event sample count of %d", kDefaultSamples);
    g_event_sample_count = kDefaultSamples;
  } else {
    g_event_sample_count = strtol(env, NULL, 10);
  }
}

void oprofile_init() {
  const int kBufSize = 512;
  int i = 0;
  char buf[kBufSize];
  char *end = buf;
  get_events();
  if (!opinitialized) {
    //if (system("opcontrol --reset")) err(-1, "reset error");
    //if (system("opcontrol --deinit")) err(-1, "deinit error");
    //if (system("opcontrol --init"))err(-1, "init error");
    //if (system("opcontrol --start-daemon")) err(-1, "start-daemon error");

    end = buf + sprintf(buf, "opcontrol ");
    for (i = 0; i < g_num_events; i++) {
      end += snprintf(end, buf + kBufSize - end, "--event=%s ", g_events[i]);
      if (end > buf + kBufSize) err(-1, "opcontrol arg buffer overflow");
    }
    if(system(buf)) err(-1, "opcontrol events");
    opinitialized = 1;
  }
}

void oprofile_start() {
  if(system("opcontrol -s")) err(-1, "starting opcontrol");
}

void oprofile_end() {
  if(system("opcontrol -t")) err(-1, "stopping opcontrol");
}

void hook_start_roi_() {
  // trash the cache
  const int kAllocSize = 48 * 1024 * 2014;
  long *tmp = malloc(kAllocSize);
  int i;
  for (i = 0; i < kAllocSize / sizeof(long); i++) tmp[i] = 0;
  free(tmp);
  struct timeval t;
  gettimeofday(&t,NULL);
  time_begin = (double)t.tv_sec+(double)t.tv_usec*1e-6;
  //if (perf_event_fds == NULL) warnx("perfctr FDs not initialized");
  //else perfctr_start(perf_event_fds, g_num_events);
  //  perfctr_start(perf_event_fds2, g_num_events);
  oprofile_start();
}

void hook_end_roi_() {
  if (finalized == 1) errx(1, "End ROI called twice");
  oprofile_end();
  if (perf_event_fds != NULL) {
    perfctr_stop(perf_event_fds, g_num_events);
    //  perfctr_stop(perf_event_fds2, g_num_events);
    perfctr_read(perf_event_fds[0], g_event_counts, g_events);
    //  perfctr_read(perf_event_fds2[0], g_event_counts+g_num_events, g_events2);
  }
  struct timeval t;
  gettimeofday(&t,NULL);
  time_end = (double)t.tv_sec+(double)t.tv_usec*1e-6;
  printf("end ROI hook, Total time spent in ROI: %.3fs\n", time_end-time_begin);
  finalized = 1;
}

void hook_thread_start_() {
  //perfctr_init();
  oprofile_init();
  //perf_event_fds = fd_group_init(g_num_events, g_events);
  //  perf_event_fds2 = fd_group_init(g_num_events, g_events2);
  __sync_fetch_and_add(&active_threads, 1);
}

void hook_thread_end_() {
  if (!finalized) hook_end_roi_();
  __sync_fetch_and_sub(&active_threads, 1);
}


void perfctr_init() {
  int i;
  if (!initialized) {
    char *buf = malloc(64);
    printf("Initializing performance counters\n");
    printf("Stack address %p, heap %p\n", &buf, buf);
    free(buf);
    get_events();
    /*
     * Initialize libpfm library (required before we can use it)
     */
    int ret = pfm_initialize();
    if (ret != PFM_SUCCESS) errx(1, "cannot initialize library: %s", pfm_strerror(ret));
    for (i = 0; i < g_num_events; i++) {
      g_event_counts[i] = 0;
    }
    pthread_mutex_init(&count_lock, NULL);
    initialized = 1;
  }
}

int *fd_group_init(int num_events, char **events) {
  struct perf_event_attr *attr;
  int fd, ret, leader_fd, i;
  int *fds = malloc(num_events * sizeof(int));
  if (!fds) err(1, "could not allocate memory");
  attr = calloc(num_events, sizeof(*attr));
  // do refs, then misses
  for (i = 0; i < num_events; i++) {
    /*
     * 1st argument: event string
     * 2nd argument: default privilege level (used if not specified in the event string)
     * 3rd argument: the perf_event_attr to initialize
     */
    ret = pfm_get_perf_event_encoding(events[i], PFM_PLM3, &attr[i], NULL, NULL);
    if (ret != PFM_SUCCESS) errx(1, "evt %d: cannot find encoding: %s", i, pfm_strerror(ret));
    printf("Using encoding %lx for event %s\n", attr[i].config, events[i]);

    attr[i].inherit = 0; // inheritance currently doesn't work with FORMAT_GROUP

    /*
     * request timing information because event may be multiplexed
     * and thus it may not count all the time. The scaling information
     * will be used to scale the raw count as if the event had run all
     * along
     */
    attr[i].read_format = PERF_FORMAT_TOTAL_TIME_ENABLED|PERF_FORMAT_TOTAL_TIME_RUNNING
                          | PERF_FORMAT_GROUP;

    /* do not start immediately after perf_event_open() */
    attr[i].disabled = 1;

    /*
     * create the event and attach to self
     * Note that it attaches only to the main thread, there is no inheritance
     * to threads that may be created subsequently.
     *
     * if mulithreaded, then getpid() must be replaced by gettid()
     */
    if (i == 0) {
      fd = perf_event_open(&attr[i], 0, -1, -1, 0);
      fds[i] = fd;
      leader_fd = fd;
    } else {
      fd = perf_event_open(&attr[i], 0, -1, leader_fd, 0);
      fds[i] = fd;
    }
    if (fd < 0) {
      warn("warning: evt %d: cannot create event", i);
      free(attr);
      free(fds);
      return NULL;
    }
  }
  free(attr);
  return fds;
}

void perfctr_start(int *fds, int count) {
  /*
   * start counting now
   */
  int i, ret;
  for (i = 0; i < count; i++) {
    ret = ioctl(fds[i], PERF_EVENT_IOC_ENABLE, 0);
    if (ret) err(1, "ioctl(enable) failed");
  }
}

void perfctr_stop(int *fds, int count) {
  /*
   * stop counting
   */
  int i, ret;
  for (i = 0; i < count; i++) {
    ret = ioctl(fds[i], PERF_EVENT_IOC_DISABLE, 0);
    if (ret) err(1, "ioctl(disable) failed");
  }
}

void perfctr_read(int fd, uint64_t *event_counts, char **events) {
  /*
   * read the count + scaling values
   *
   * It is not necessary to stop an event to read its value
   */
  int ret, i;
  uint64_t values[g_num_events + 3];
  uint64_t counts[g_num_events];
  double runtime;

  ret = read(fd, values, sizeof(values));
  if (ret != sizeof(values)) {
    if (ret == -1) {
      err(1, "cannot read event values" );
    }
    else {    /* likely pinned and could not be loaded */
      warnx("could not read all events, ret=%d", ret);
    }
  }

  if (values[0] != g_num_events) {
    errx(1, "event count read was %"PRIu64", expected %d", values[0], g_num_events);
  }

  /*
   * scale count
   *
   * values[0] = number of counters
   * values[1] = TIME_ENABLED
   * values[2] = TIME_RUNNING
   * * values[3 - kEventCount+2] = raw counts
   */
  if (values[2]) {
    printf("Enabled %"PRIu64", Running %"PRIu64"\n", values[1], values[2]);
    for (i = 0; i < g_num_events; i++) {
      counts[i] = (uint64_t)((double)values[3 + i] * values[1]/values[2]);
    }
  } else {
    warn("values[2] is 0");
  }

  runtime = (double)values[2]/values[1] * 100.0;

  pthread_mutex_lock(&count_lock);
  for (i = 0; i < g_num_events; i++) {
    printf("evt %s count=%'"PRIu64", ", events[i], counts[i]);
    event_counts[i] += counts[i];
  }
  printf("runtime %.1f%%\n", runtime);

  printf("ratio %.4f%%\n", (double) counts[1] / counts[0] * 100.0);
  if (g_num_events > 3) {
    printf("ratio %.4f%%\n", (double) counts[3] / counts[2] * 100.0);
  }
  pthread_mutex_unlock(&count_lock);

  close(fd);
}

void perfctr_print(uint64_t *event_counts, char **events) {
  int i;
  /* Print final count */
  if (active_threads != 0) printf("%d active threads\n", active_threads);
  //  printf("Final totals: evt %s count=%'"PRIu64", evt %s count=%'"PRIu64"\n",
  //      events[0], event_counts[0], events[1], event_counts[1]);
  //  printf("ratio %.4f%%\n", (double) event_counts[1] / event_counts[0] * 100.0);

  for (i = 0; i < g_num_events; i++) {
    printf("evt %s count= %'"PRIu64"\n", events[i], event_counts[i]);
  }

  printf("ratio %.4f%%\n", (double) event_counts[1] / event_counts[0] * 100.0);
  if (g_num_events > 3) {
    printf("ratio %.4f%%\n", (double) event_counts[2] / event_counts[0] * 100.0);
    printf("ratio %.4f%%\n", (double) event_counts[3] / event_counts[0] * 100.0);
  }
  printf("counts ");
  for (i = 0; i < g_num_events; i++) {
    printf("%'"PRIu64"\t", event_counts[i]);
  }
  printf("\n");

}
