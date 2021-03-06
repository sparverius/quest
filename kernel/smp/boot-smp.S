/* -*- Mode: asm; comment-start: "\/*"; comment-end: "*\/"; indent-tabs-mode: nil -*- */
/*                    The Quest Operating System
 *  Copyright (C) 2005-2010  Richard West, Boston University
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

#include "kernel.h"
#include "smp/smp.h"
        .text
        .align 0x10
        .code16

        /* This code gets loaded by smp.c into address MP_BOOTADDR
         * for the application processor to start. */

        /* These symbols are linked at segment 0x10100 but will be later
         * placed at segment MP_BOOTADDR>>4, so some care has to be taken with
         * addresses */
        .globl patch_code_start
        .globl patch_code_end
        .globl status_code
        .globl ap_stack_ptr
patch_code_start:
        jmp 1f

status_code:    
        .long 0                /* status indicator */
ap_stack_ptr:                  /* C stack for AP */
        .long 0                

1: 
        /* Entering in 16-bit real mode */
        movw %cs, %dx
        movw %dx, %ds
        
        lgdtl patch_gdt_ptr - patch_code_start /* using ds segment */

        xorw %dx, %dx
        movw %dx, %ds

        movl %cr4, %eax /* EAX is temporary for CR4 */
        orl $0x10, %eax /* Set PSE bit of CR4 */
        movl %eax, %cr4

        movl $pgd, %eax
        movl %eax, %cr3
        movl %cr0, %eax /* need to set bit 31 of CR0 - see 3-18 in Manual vol 3 */
        orl $0x80000001, %eax
        movl %eax, %cr0 /* and enter protected mode as well */

        /* flush icache */
        ljmpl $0x08, $ap_pmode - patch_code_start + MP_BOOTADDR

        .align 0x10
        .code32
ap_pmode:  
        movw $0x10, %ax /* Set DS-SS to refer to KERNEL DS */
        movw %ax, %ds
        movw %ax, %es
        movw %ax, %fs
        movw %ax, %gs
        movw %ax, %ss
                
        xorw %ax, %ax
        lldt %ax        


        /* setup our stack */
        movl $ap_stack_ptr - patch_code_start + MP_BOOTADDR, %eax
        movl (%eax), %eax
        addl $0x1000, %eax
        movl %eax, %esp

        /* Allocate pages and GDT entry to setup per-CPU memory space */
        lcall $0x8, $percpu_per_cpu_init

        /* Write 1 to status indicator */
        movl $status_code - patch_code_start + MP_BOOTADDR, %eax
        movl $0x1, (%eax)
        
        /* call back into C */
        lcall $0x8, $ap_init
        
patch_gdt_ptr:  
        .short 0x7FF     /* length in bytes - 256 descriptors */
        .long  KERN_GDT   /* linear address */
patch_code_end: 

/* vi: set et sw=8 sts=8: */
