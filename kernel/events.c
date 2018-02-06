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

#include "rt/rt_api.h"
#include "stdio.h"

RT_FC_TINY_DATA rt_event_sched_t   __rt_sched;
RT_FC_TINY_DATA rt_event_t        *__rt_first_free = NULL;

void rt_event_sched_init(rt_event_sched_t *sched)
{
  sched->first = NULL;
  sched->waiting = NULL;
}

void __rt_event_init(rt_event_t *event, rt_event_sched_t *sched)
{
  __rt_event_min_init(event);
  event->sched = sched;
  event->callback = NULL;
}

int rt_event_alloc(rt_event_sched_t *sched, int nb_events)
{
  int irq = hal_irq_disable();

  if (!sched) sched = __rt_thread_current->sched;

  int flags;
  if (rt_is_fc()) flags = RT_ALLOC_FC_DATA;
  else flags = RT_ALLOC_CL_DATA + rt_cluster_id();

  rt_event_t *event = (rt_event_t *)rt_alloc(flags, sizeof(rt_event_t)*nb_events);
  if (event == NULL) return -1;

  for (int i=0; i<nb_events; i++) {
    __rt_event_init(event, sched);
    event->next = __rt_first_free;
    __rt_first_free = event;
    event++;
  }

  hal_irq_restore(irq);
  return 0;
}

void rt_event_free(rt_event_sched_t *sched, int nb_events)
{
  for (int i=0; i<nb_events; i++)
  {
    rt_event_t *event = __rt_first_free;
#if PULP_CHIP == CHIP_GAP
    rt_free(RT_ALLOC_PERIPH, (void *)event->copy.periph_data, RT_PERIPH_COPY_PERIPH_DATA_SIZE);
#endif
    __rt_first_free = event->next;   
    rt_free(RT_ALLOC_FC_DATA, (void *)event, sizeof(rt_event_t));
  }
}

static inline __attribute__((always_inline)) void __rt_enqueue_event_to_sched(rt_event_sched_t *sched, rt_event_t *event)
{
  event->next = NULL;
  if (sched->first == NULL) {
    sched->first = event;
  } else {
    sched->last->next = event;
  }
  sched->last = event;
}

static inline __attribute__((always_inline)) void __rt_wakeup_thread(rt_event_sched_t *sched)
{
  rt_thread_t *thread = sched->waiting;
  if (thread) {
    sched->waiting = NULL;
    __rt_thread_enqueue_ready_check(thread);
  }
}

static inline __attribute__((always_inline)) void __rt_push_event(rt_event_sched_t *sched, rt_event_t *event)
{
  // Enqueue the event into the scheduler tail
  __rt_enqueue_event_to_sched(sched, event);

  // Then maybe wakeup a waiting thread
  __rt_wakeup_thread(sched);
}

static inline __attribute__((always_inline)) rt_event_t *__rt_get_event(rt_event_sched_t *sched, void (*callback)(void *), void *arg)
{
  // Get event from scheduler and initialize it
  rt_event_t *event = __rt_first_free;
  if (event == NULL) return NULL;
  __rt_first_free = event->next;
  event->callback = callback;
  event->arg = arg;
  return event;
}

rt_event_t *rt_event_get(rt_event_sched_t *sched, void (*callback)(void *), void *arg)
{
  int irq = hal_irq_disable();
  if (!sched) sched = __rt_thread_current->sched;
  rt_event_t *event = __rt_get_event(sched, callback, arg);
  if (event) event->sched = sched;
  hal_irq_restore(irq);
  return event;
}

rt_event_t *rt_event_get_blocking(rt_event_sched_t *sched)
{
  int irq = hal_irq_disable();
  if (!sched) sched = __rt_thread_current->sched;
  rt_event_t *event = __rt_get_event(sched, NULL, NULL);
  if (event) {
    event->sched = sched;
    event->pending = 1;
  }
  hal_irq_restore(irq);
  return event;
}

void rt_event_push(rt_event_t *event)
{
  int irq = hal_irq_disable();
  __rt_push_event(event->sched, event);
  hal_irq_restore(irq);
}

int rt_event_push_callback(rt_event_sched_t *sched, void (*callback)(void *), void *arg)
{
  int irq = hal_irq_disable();
  rt_event_t *event = __rt_get_event(sched, callback, arg);
  if (event == NULL) return -1;
  __rt_push_event(sched, event);
  hal_irq_restore(irq);
  return 0;
}

void __rt_event_unblock(rt_event_t *event)
{
  event->pending = 0;
  rt_thread_t *thread = event->thread;
  if (thread) {
    __rt_thread_enqueue_ready_check(thread);
  }
}

void __rt_event_execute(rt_event_sched_t *sched, int wait)
{
  if (sched == NULL) sched = __rt_thread_current->sched;
  rt_event_t *event = sched->first;

  if (event == NULL) {
    if (wait) {
      // Pop first event from the queue. Loop until we pop a null event
      // We must always read again the queue head, as the executed
      // callback can modify the queue 

      sched->waiting = __rt_thread_current;

      do {
        if (__rt_ready_queue.first) {
          __rt_thread_sleep();
        }
        else {
          rt_wait_for_interrupt();
          hal_irq_enable();
          hal_irq_disable();
        }
        event = *((rt_event_t * volatile *)&sched->first);
      } while (!event);
    } else {
      hal_irq_enable();
      return;
    }
  }

  do {
    sched->first = event->next;

    // Read event information and put it back in the scheduler
    void (*callback)(void *) = event->callback;
    void *arg = event->arg;

    // Free the event now so that it can be used directly from the callback
    if (!event->pending) {
      event->next = __rt_first_free;
      __rt_first_free = event;
    }

    // Finally execute the event with interrupts enabled
    if (callback) {
      hal_irq_enable();
      callback(arg);
      hal_irq_disable();
    }

    __rt_event_unblock(event);

    event = sched->first;

  } while(event);

}

void __rt_wait_event(rt_event_t *event)
{
  while (event->pending) {
    event->thread = __rt_thread_current;
    __rt_event_execute(__rt_thread_current->sched, 1);
  }

  rt_event_sched_t *sched = event->sched;
  if (sched) {
    event->next = __rt_first_free;
    __rt_first_free = event;
  }
}

void rt_event_wait(rt_event_t *event)
{
  int irq = hal_irq_disable();
__rt_wait_event(event);
  hal_irq_restore(irq);
}

RT_FC_BOOT_CODE void __attribute__((constructor)) __rt_event_sched_init()
{
  rt_event_sched_init(&__rt_sched);
}
