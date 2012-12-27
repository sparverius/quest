/*                    The Quest Operating System
 *  Copyright (C) 2005-2012  Richard West, Boston University
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "arch/i386.h"
#include "arch/i386-percpu.h"
#include "arch/i386-measure.h"
#include "kernel.h"
#include "mem/mem.h"
#include "util/elf.h"
#include "fs/filesys.h"
#include "smp/smp.h"
#include "smp/apic.h"
#include "util/printf.h"
#include "util/screen.h"
#include "util/debug.h"
#include "sched/sched.h"
#include "sched/vcpu.h"

#ifdef USE_VMX
#include "vm/shm.h"
#include "vm/spow2.h"
#include "vm/vmx.h"

#ifdef USE_LINUX_SANDBOX
#include "vm/linux_boot.h"
#endif

#endif

#define DEBUG_LINUX_BOOT

#ifdef DEBUG_LINUX_BOOT
#define DLOG(fmt,...) DLOG_PREFIX("Linux boot",fmt,##__VA_ARGS__)
#else
#define DLOG(fmt,...) ;
#endif

/*
 * Load Linux kernel bzImage from pathname in filesystem to virtual
 * address load_addr in memory.
 */
int
load_linux_kernel (uint32 * load_addr, char * pathname)
{
#ifdef USE_LINUX_SANDBOX
  int act_len = 0;
  linux_setup_header_t * setup_header;
  int eztftp_bulk_read (char *, uint32 *);

  act_len = eztftp_bulk_read (pathname, load_addr);

  if (act_len < 0) {
    DLOG ("Linux kernel load failed!");
    return -1;
  }
  DLOG ("Linux kernel size: 0x%X", act_len);

  setup_header = (linux_setup_header_t *) (((uint8 *) load_addr) + LINUX_SETUP_HEADER_OFFSET);

  DLOG ("---------------------------");
  DLOG ("| DUMP LINUX SETUP HEADER |");
  DLOG ("---------------------------");
  DLOG (" ");
  DLOG ("setup_sects: 0x%X", setup_header->setup_sects);
  DLOG ("root_flags: 0x%X", setup_header->root_flags);
  DLOG ("syssize: 0x%X", setup_header->syssize);
  DLOG ("ram_size: 0x%X", setup_header->ram_size);
  DLOG ("vid_mode: 0x%X", setup_header->vid_mode);
  DLOG ("root_dev: 0x%X", setup_header->root_dev);
  DLOG ("boot_flag: 0x%X", setup_header->boot_flag);
  DLOG ("jump: 0x%X", setup_header->jump);
  DLOG ("header: 0x%X", setup_header->header);
  DLOG ("version: 0x%X", setup_header->version);
  DLOG (" ");
#endif

  return act_len;
}

static u32 boot_thread_stack[1024] ALIGNED (0x1000);

#ifdef DEBUG_LINUX_BOOT
static task_id boot_thread_id = 0;
#endif

/* Input parameter for VM-Exit */
static struct _linux_boot_param {
  uint32 kernel_addr;
  int size;
} exit_param;


static void
linux_boot_thread (void)
{
#ifdef DEBUG_LINUX_BOOT
  int cpu;
  cpu = get_pcpu_id ();
#endif
  extern void * vm_exit_input_param;

  DLOG ("Linux boot thread started in sandbox %d", cpu);

  unlock_kernel ();
  sti ();

  /* TODO: Wait for network initialization here. Other drivers should already be ready. */
  tsc_delay_usec (3000000);
  DLOG ("Loading Linux kernel bzImage...");
  cli ();
  lock_kernel ();
  exit_param.size = load_linux_kernel ((uint32 *) LINUX_KERNEL_LOAD_VA, "/boot/vmlinuz");

  if (exit_param.size == -1) {
    DLOG ("Loading failed.");
    goto finish;
  }

  /* Now, trap into monitor and setup Linux VMCS */
  exit_param.kernel_addr = LINUX_KERNEL_LOAD_VA;
  vm_exit_input_param = (void *) &exit_param;
  vm_exit (VM_EXIT_REASON_LINUX_BOOT);

  /* TODO: Go into real mode with ljmp to 0x90200 */
  /* disable protected mode */
  uint32 * dump;
  dump = (uint32 *) 0x8000;
  int i = 0;
  com1_printf ("-------------------DUMP------------------------\n");
  for (i = 0; i < 10; i++) {
    com1_printf ("%X ", dump[i]);
  }
  com1_printf ("\n-------------------DUMP------------------------\n");
  asm volatile ("jmp 0x8000");
  unlock_kernel ();
  sti ();

  finish:
  exit_kernel_thread ();
}

bool
linux_boot_thread_init (void)
{
#ifdef DEBUG_LINUX_BOOT
  boot_thread_id =
#endif
  start_kernel_thread ((uint32) linux_boot_thread,
                       (uint32) &boot_thread_stack, "Linux boot thread");
  return TRUE;
}

/* 
 * Local Variables:
 * indent-tabs-mode: nil
 * mode: C
 * c-file-style: "gnu"
 * c-basic-offset: 2
 * End: 
 */

/* vi: set et sw=2 sts=2: */