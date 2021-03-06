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

#if defined(ARCHI_HAS_FC)

static unsigned long long timer_count;



static int __rt_time_poweroff(void *arg)
{
  // Remember the current timer count so that we can restore it
  // when the system is powered-on
  timer_count = hal_timer_count_get_64(hal_timer_fc_addr(0, 0));

  return 0;
}

static int __rt_time_poweron(void *arg)
{
  // Restore the timer count we saved before shutdown
  hal_timer_count_set_64(hal_timer_fc_addr(0, 0), timer_count);

  return 0;
}

unsigned long long rt_time_get_us()
{
  // Get 64 bit timer counter value and convert it to microseconds
  // as the timer input is connected to the ref clock.
  unsigned long long count = hal_timer_count_get_64(hal_timer_fc_addr(0, 0));
  if ((count >> 32) == 0) return count * 1000000 / ARCHI_REF_CLOCK;
  else return count / ARCHI_REF_CLOCK * 1000000;
}

RT_FC_BOOT_CODE void __attribute__((constructor)) __rt_time_init()
{
  int err = 0;

  // Configure the FC timer in 64 bits mode as it will be used as a common
  // timer for all virtual timers.
  // We also use the ref clock to make the frequency stable.
  hal_timer_conf(
    hal_timer_fc_addr(0, 0), PLP_TIMER_ACTIVE, PLP_TIMER_RESET_ENABLED,
    PLP_TIMER_IRQ_DISABLED, PLP_TIMER_IEM_DISABLED, PLP_TIMER_CMPCLR_DISABLED,
    PLP_TIMER_ONE_SHOT_DISABLED, PLP_TIMER_REFCLK_ENABLED,
    PLP_TIMER_PRESCALER_DISABLED, 0, PLP_TIMER_MODE_64_ENABLED
  );

  err |= __rt_cbsys_add(RT_CBSYS_POWEROFF, __rt_time_poweroff, NULL);

  err |= __rt_cbsys_add(RT_CBSYS_POWERON, __rt_time_poweron, NULL);

  if (err) rt_fatal("Unable to initialize time driver\n");
}

#endif
