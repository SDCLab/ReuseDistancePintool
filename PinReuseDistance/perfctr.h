/*
 * perfctr.h
 *
 *  Created on: Apr 13, 2010
 *      Author: dschuff
 */

#ifndef PERFCTR_H_
#define PERFCTR_H_
#include <inttypes.h>

void perfctr_init();
int *fd_group_init(int num_events, char **events);
void perfctr_start(int *fds, int count);
void perfctr_stop(int *fds, int count);
void perfctr_read(int fd, uint64_t *event_counts, char **events);
void perfctr_print(uint64_t *event_counts, char **events);

#endif /* PERFCTR_H_ */
