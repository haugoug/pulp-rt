/*
 * Copyright (C) 2018 ETH Zurich and University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * Authors: Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#ifndef __OMPRT_H__
#define __OMPRT_H__

#include <rt/rt_api.h>
#include "omp.h"
#include <stdarg.h>

#define OMP_NB_WORKSHARE_BITS 3
#define OMP_NB_WORKSHARE (1<<OMP_NB_WORKSHARE_BITS)

struct omp_ws_s;

typedef struct ompTask_s {
  void (*func)(void *);
  void *data;
  struct ompTask_s *next;
} ompTask_t;

typedef struct {
  //int id;
  //int barrier;
  //ompTask_t *firstReadyTask;
  //ompTask_t *lastReadyTask;
  char nbThreads;
  //char hasTasks;
#if EU_VERSION == 1
  plp_swMutex_t mutex;
#endif
} omp_team_t;

typedef struct {
  //ompTask_t *taskPool;
  omp_team_t plainTeam;
#ifdef __LLVM__
  int numThreads;
#endif
  unsigned short coreMask;
} omp_t;

extern omp_t RT_L1_TINY_DATA ompData;

void partialParallelRegion(void (*fn) (void*), void *data, int num_threads);

static inline void perfInitAndStart()
{
#ifdef __PROFILE0__
#ifdef OR10N4
  cpu_perf_conf_events(SPR_PCER_ALL_EVENTS_MASK);
  cpu_perf_setall(0);
  cpu_perf_conf(SPR_PCMR_ACTIVE | SPR_PCMR_SATURATE);
#endif
#endif
}

static inline void perfParallelEnter()
{
#ifdef __PROFILE0__
#ifdef __PROFILE1__
  pulp_trace_perf(TRACE_OMP_PARALLEL_ENTER);
#else
  pulp_trace(TRACE_OMP_PARALLEL_ENTER);
#endif
#endif
}
 
static inline void perfParallelExit()
{
#ifdef __PROFILE0__
#ifdef __PROFILE1__
      pulp_trace_perf(TRACE_OMP_PARALLEL_EXIT);
#else
      pulp_trace(TRACE_OMP_PARALLEL_EXIT);
#endif
#endif
}

int omp_init(omp_t *_this);
int omp_start(omp_t *_this, int (*entry)(void *), void *args);

static inline int getThreadNum(omp_t *omp)
{
  return rt_core_id();
}

#ifdef __GNUC__
static inline ompTask_t *allocTask(omp_t *_this, int arg_size) 
{
#if 0
  ompTask_t *task = _this->taskPool;
  if (task == NULL) return NULL;
  _this->taskPool = task->next;
  return task;
#else
  //return (ompTask_t *)rt_l1_alloc(rtCc, sizeof(ompTask_t)+arg_size);
  return (void *)0;
#endif
}
#endif

static inline void freeTask(omp_t *_this, ompTask_t *task) 
{
#if 1
  //task->next = _this->taskPool;
  //_this->taskPool = task;
#else
  rt_l1_free(rtCc, (unsigned int)task);
#endif
}

// This returns OMP data in 3 instructions (address construction + load)
static inline omp_t *omp_getData() {
  return (omp_t *)&ompData;
  //return (omp_t *)plp_getClusterUserData();
}

#if 0
  //return (omp_t *)(rt_pm_getDesc(&rtCc->msgHandler, PULP_REQ_OMP_PMID)->data);
static inline void omp_setData(rt_cc_t *rtCc, omp_t *_this) {
  rt_pm_getDesc(&rt_getCcData(rt_clusterId())->msgHandler, PULP_REQ_OMP_PMID)->data = (void *)_this;
}
#endif

static inline omp_team_t *getTeam(omp_t *_this) {
  return &_this->plainTeam;
}

static inline omp_team_t *getCurrentTeam() {
  return getTeam(omp_getData());
}

static inline void criticalStart(omp_team_t *team)
{
#if EU_VERSION >= 3
  eu_mutex_lock(eu_mutex_addr(0));
#else
  plp_swMutex_lock(&team->mutex);
#endif
}

static inline void userCriticalStart(omp_team_t *team)
{
#ifdef __PROFILE0__
  pulp_trace(TRACE_OMP_CRITICAL_ENTER);
#endif
  criticalStart(team);
}

static inline void criticalEnd(omp_team_t *team)
{
#if EU_VERSION >= 3
  eu_mutex_unlock(eu_mutex_addr(0));
#else
  plp_swMutex_unlock(&team->mutex);
#endif
}

static inline void userCriticalEnd(omp_team_t *team)
{
  criticalEnd(team);
#ifdef __PROFILE0__
  pulp_trace(TRACE_OMP_CRITICAL_EXIT);
#endif
}

static inline __attribute__((always_inline)) void doBarrier(omp_team_t *team) {
#if EU_VERSION >= 3
  eu_bar_trig_wait_clr(eu_bar_addr(0));
#else
  pulp_barrier_notify(0);
  pulp_evt_wait();
  pulp_gpevt_clear(0);
#endif
}

static inline void userBarrier(omp_team_t *team) {
#ifdef __PROFILE0__
  pulp_trace(TRACE_OMP_BARRIER_ENTER);
#endif
  doBarrier(team);
#ifdef __PROFILE0__
  pulp_trace(TRACE_OMP_BARRIER_EXIT);
#endif
}

static inline void __attribute__((always_inline)) parallelRegionExec(void *data, void (*fn) (void*))
{
#if EU_VERSION >= 3
  // Now that the team is ready, wake up slaves
  eu_dispatch_push((unsigned int)fn);
  eu_dispatch_push((unsigned int)data);
#else
  plp_dispatch_push2((1<<getCurrentTeam()->nbThreads)-1, (unsigned int)fn, (unsigned int)data);
#endif

  // Team execution
  perfParallelEnter();  
#ifdef __GNUC__
  fn(data);
#else
  kmpc_micro entry = (kmpc_micro)fn;
  int id;
  entry(&id, &id, data);
#endif
  perfParallelExit();
  
  // Execute the final barrier to wait until the slaves have finished the team execution
  doBarrier((void *)0);
}

static inline void __attribute__((always_inline)) parallelRegion(void *data, void (*fn) (void*), int num_threads)
{
  int coreMask = omp_getData()->coreMask;

  if (((1 << num_threads) & coreMask) <= 1) {
    // We differentiate plain team and partial teams to put more optimizations on plain teams
    // as they are the most used
    parallelRegionExec(data, fn);
  } else {
    partialParallelRegion(fn, data, num_threads);
  }
}

static inline __attribute__((always_inline)) unsigned int sectionGet()
{
#if EU_VERSION >= 3
  int size = eu_loop_getChunk(eu_loop_addr(0));
  int start = eu_loop_getStart(eu_loop_addr(0));
  if (size == 0) return 0;
  else return start;
#else
  return 0;
#endif
}

static inline __attribute__((always_inline)) unsigned int sectionInit(unsigned int count)
{
#if EU_VERSION >= 3
  unsigned int state = eu_loop_getState(eu_loop_addr(0));
  if (state == EU_LOOP_DONE) goto end;
  else if (state == EU_LOOP_SKIP) return 0;
  eu_loop_setStart(eu_loop_addr(0), 1);
  eu_loop_setEnd(eu_loop_addr(0), count+1);
  eu_loop_setIncr(eu_loop_addr(0), 1);
  eu_loop_setChunk(eu_loop_addr(0), 1);
end:
  return sectionGet();
#else
  return 0;
#endif
}

static inline __attribute__((always_inline)) int dynLoopIter(omp_team_t *team, int *istart, int *iend, int *isLast)
{
#if EU_VERSION >= 3
  int size = eu_loop_getChunk(eu_loop_addr(0));
  int start = eu_loop_getStart(eu_loop_addr(0));
  if (size == 0) goto end;
  *istart = start;
  *iend = start + size;
  if (isLast) *isLast = 0;
  return 1;
end:
  return 0;
#else
  return 0;
#endif
}

static inline int dynLoopInit(omp_team_t *team, int start, int end, int incr, int chunk_size, int *istart, int *iend)
{
#if EU_VERSION >= 3
  unsigned int state = eu_loop_getState(eu_loop_addr(0));
  // Case where the loop is already initialized and still have available indexes
  if (state == EU_LOOP_DONE) goto end;
  // Case where the loop is already over
  else if (state == EU_LOOP_SKIP) return 0;
  // Case where we are the first to enter the loop
  eu_loop_setStart(eu_loop_addr(0), start);
  eu_loop_setEnd(eu_loop_addr(0), end);
  eu_loop_setIncr(eu_loop_addr(0), incr);
  eu_loop_setChunk(eu_loop_addr(0), chunk_size);

end:
  return dynLoopIter(team, istart, iend, (void *)0);
#else
  return 0;
#endif
}

static inline void dynLoopInitNoIter(omp_team_t *team, int start, int end, int incr, int chunk_size)
{
#if EU_VERSION >= 3
  unsigned int state = eu_loop_getState(eu_loop_addr(0));
  // Case where the loop is already initialized and still have available indexes
  if (state == EU_LOOP_DONE) return;
  // Case where the loop is already over
  else if (state == EU_LOOP_SKIP) return;
  // Case where we are the first to enter the loop
  eu_loop_setStart(eu_loop_addr(0), start);
  eu_loop_setEnd(eu_loop_addr(0), end);
  eu_loop_setIncr(eu_loop_addr(0), incr);
  eu_loop_setChunk(eu_loop_addr(0), chunk_size);
#endif
}

static inline void dynLoopInitSingle(omp_team_t *team, int start, int end, int incr, int chunk_size, int nbThread)
{
#if EU_VERSION >= 3
  eu_loop_getState(eu_loop_addr(0));
  eu_loop_setStart(eu_loop_addr(0), start);
  eu_loop_setEnd(eu_loop_addr(0), end);
  eu_loop_setIncr(eu_loop_addr(0), incr);
  eu_loop_setChunk(eu_loop_addr(0), chunk_size);
#endif
}

static inline int singleStart()
{
#if EU_VERSION >= 3
  return eu_loop_getSingle(eu_loop_addr(0));
#else
  return 0;
#endif
}

#endif
