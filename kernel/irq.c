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
 * Copyright (C) 2018 GreenWaves Technologies
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
 * Authors: Eric Flamand, GreenWaves Technologies (eric.flamand@greenwaves-technologies.com)
 *          Germain Haugou, ETH (germain.haugou@iis.ee.ethz.ch)
 */

#include "rt/rt_api.h"

#if defined(RISCV_VERSION)

static unsigned int __rt_get_itvec(unsigned int ItBaseAddr, unsigned int ItIndex, unsigned int ItHandler)

{
  /* Prepare 32bit container to be stored at
   *(ItBaseAddr+ItIndex) containing a relative jump from
    (ItBaseAddr+ItIndex) to Handler */

  unsigned int S = ((unsigned int) ItHandler - (ItBaseAddr+ItIndex*4));
  unsigned int R = 0x6F; /* Jal opcode with x0 as target, eg no return */

  /* Forge JAL x0, Address: with Address = S => Bin[31:0] = [S20 
  | S10:1 | S11 | S19:12 | 00000 01101111] */

  R = __BITINSERT(R, __BITEXTRACT(S,  1, 20),  1, 31);
  R = __BITINSERT(R, __BITEXTRACT(S, 10,  1), 10, 21);
  R = __BITINSERT(R, __BITEXTRACT(S,  1, 11),  1, 20);
  R = __BITINSERT(R, __BITEXTRACT(S,  8, 12),  8, 12);

  return R;
}

void rt_irq_set_handler(int irq, void (*handler)())
{
  unsigned int base = __rt_get_fc_vector_base();

  unsigned int jmpAddr = base + 0x4 * irq;

  *(volatile unsigned int *)jmpAddr = __rt_get_itvec(base, irq, (unsigned int)handler);

  //selective_flush_icache_addr(jmpAddr & ~(ICACHE_LINE_SIZE-1));

  //if (!rt_is_fc() || plp_pmu_cluster_isOn(0)) flush_all_icache_banks_common(plp_icache_cluster_remote_base(0));

#if defined(PLP_FC_HAS_ICACHE)
  flush_all_icache_banks_common(plp_icache_fc_base());
#endif
  
}

#else

void rt_irq_set_handler(int irq, void (*handler)())
{
}

#endif

#include <stdio.h>
#include <stdlib.h>

void __attribute__((weak)) illegal_insn_handler_c()
{

}

void __rt_handle_illegal_instr()
{
  unsigned int mepc = hal_mepc_read();
  rt_warning("Reached illegal instruction (PC: 0x%x, opcode: 0x%x\n", mepc, *(int *)mepc);
  illegal_insn_handler_c();
}

RT_BOOT_CODE void __attribute__((constructor)) __rt_irq_init()
{

#if defined(ARCHI_HAS_FC)
  // As the FC code may not be at the beginning of the L2, set the 
  // vector base to get proper interrupt handlers
  __rt_set_fc_vector_base((int)rt_irq_vector_base());
#endif

#if defined(ARCHI_HAS_FC_EU)
  // If the FC has an event unit, the wait-for-interrupt will be emulated.
  // Activate all events to wake-up for every incoming interrupts
  eu_evt_maskSet(0xffffffff);
#endif
}
