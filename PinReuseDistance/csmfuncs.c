#include <stdio.h>
#include <sys/time.h>
#include "csm.h"

void cfunc_(int * arg){
	printf("Called cfunc_ with arg %p\n", arg);
	fflush(stdout);
	printf("*arg = %d\n", *arg);
}

static __thread double time_begin = 0;
static __thread double time_end = 0;

void __attribute__((weak, noinline, externally_visible)) hook_start_roi_() {
	asm("");  /* prevent calls from being optimized away*/
	struct timeval t;
	gettimeofday(&t,NULL);
	time_begin = (double)t.tv_sec+(double)t.tv_usec*1e-6;
}

void __attribute__((weak, noinline, externally_visible)) hook_end_roi_() {
	asm("");
	struct timeval t;
	gettimeofday(&t,NULL);
	time_end = (double)t.tv_sec+(double)t.tv_usec*1e-6;
	printf("end ROI hook, Total time spent in ROI: %.3fs\n", time_end-time_begin);
}

void __attribute__((weak, noinline, externally_visible)) hook_thread_start_() {
	printf("Thread start hook, no perfctrs\n");
}

void __attribute__((weak, noinline, externally_visible)) hook_thread_end_() {
	printf("Thread end hook, no perfctrs\n");
}

void __attribute__((weak)) csm_start_sim_timer_() {
	hook_start_roi_();
	start_sim_timer();
}

void __attribute__((weak)) csm_stop_sim_timer_()  {
	stop_sim_timer();
	hook_end_roi_();
}

void csm_decobject_(int *start, int *len) {
	csm_decobject(start, *len);
}

void csm_decchunklen_(int * start, int *len, int *chunksize) {
	csm_decchunklen(start, *len, *chunksize);
}

void csm_decchunknum_(int *start, int *len, int *chunks) {
	csm_decchunknum(start, *len, *chunks);
}

void csm_declare_stack_(){
	csm_declare_stack();
}

void csm_prefetch_(void *addr) {
	__builtin_prefetch(addr);
}
