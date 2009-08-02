#include "arch/i386.h"
#include "kernel.h"
#include "mem/mem.h"
#include "smp/smp.h"
#include "smp/apic.h"
#include "smp/spinlock.h"
#include "drivers/acpi/acpi.h"
#include "drivers/acpi/acmacros.h"
#include "drivers/acpi/acexcep.h"
#include "util/printf.h"

static int process_acpi_tables (void);
static int acpi_add_processor (ACPI_MADT_LOCAL_APIC *);

/*******************************************************************
 * Support for ACPI via ACPICA, the Intel Reference Implementation *
 *******************************************************************/

/* ACPICA supports the notion of "Early Initialization" the very
 * purpose of which is to obtain the ACPI tables and begin the process
 * of booting Application Processors when the rest of the operating
 * system is not ready for normal operation.  This is intended to
 * supersede the usage of the Intel Multiprocessing Specification. */

/* ACPICA early initialization requires some static space be set aside
 * for ACPI tables -- and there is no dynamic memory allocation
 * available at this stage, so here it is: */
#define ACPI_MAX_INIT_TABLES 16
static ACPI_TABLE_DESC TableArray[ACPI_MAX_INIT_TABLES];

uint32 
acpi_early_init(void)
{
  return process_acpi_tables ();
}

void
acpi_secondary_init(void)
{
  ACPI_STATUS Status;
  ACPI_HANDLE SysBusHandle;
  ACPI_STATUS DisplayOneDevice (ACPI_HANDLE, UINT32, void *, void **);

  /* Complete the ACPICA initialization sequence */

  Status = AcpiInitializeSubsystem ();
  if (ACPI_FAILURE (Status)) {
    com1_printf ("Failed to initialize ACPI.\n");
  }
  Status = AcpiReallocateRootTable ();
  if (ACPI_FAILURE (Status)) {
    com1_printf ("Failed: AcpiReallocateRootTable.\n");
  }
  Status = AcpiLoadTables ();
  if (ACPI_FAILURE (Status)) {
    com1_printf ("Failed: AcpiLoadTables.\n");
  }
  Status = AcpiEnableSubsystem (ACPI_FULL_INITIALIZATION);
  if (ACPI_FAILURE (Status)) {
    com1_printf ("Failed: AcpiEnableSubsystem.\n");
  }
  Status = AcpiInitializeObjects (ACPI_FULL_INITIALIZATION);
  if (ACPI_FAILURE (Status)) {
    com1_printf ("Failed: AcpiInitializeObjects.\n");
  }

  /* Walk the System Bus "\_SB_" and output info about each object
   * found. */

  AcpiGetHandle (ACPI_ROOT_OBJECT, ACPI_NS_SYSTEM_BUS, &SysBusHandle);
  AcpiWalkNamespace (ACPI_TYPE_DEVICE, SysBusHandle, INT_MAX,
                     DisplayOneDevice, NULL, NULL);
}

#define printf com1_printf
static int
process_acpi_tables (void)
{
  extern uint32 mp_LAPIC_addr;
  extern uint32 mp_num_IOAPICs;
  extern uint32 mp_IOAPIC_addr;
  extern mp_IOAPIC_info mp_IOAPICs[];
  extern uint32 mp_num_overrides;
  extern mp_int_override mp_overrides[];

  ACPI_STATUS status;

  status = AcpiInitializeTables (TableArray, ACPI_MAX_INIT_TABLES, TRUE);
  if (status == AE_OK) {
    ACPI_TABLE_MADT *madt;
    ACPI_TABLE_FADT *fadt;
    ACPI_TABLE_BOOT *boot;
    ACPI_TABLE_ASF *asf;
    ACPI_TABLE_MCFG *mcfg;
    ACPI_TABLE_HPET *hpet;
    ACPI_TABLE_TCPA *tcpa;

    mp_ISA_bus_id = 0;
    if (AcpiGetTable (ACPI_SIG_MADT, 0, (ACPI_TABLE_HEADER **) & madt) ==
        AE_OK) {
      /* Multiple APIC Description Table */
      uint8 *ptr, *lim = (uint8 *) madt + madt->Header.Length;
      printf ("ACPI OEM: %.6s Compiler: %.4s LAPIC: %p Flags:%s\n",
              madt->Header.OemId,
              madt->Header.AslCompilerId,
              madt->Address,
              (madt->Flags & ACPI_MADT_PCAT_COMPAT) ? " PCAT_COMPAT" : "");
      mp_LAPIC_addr = madt->Address;
      ptr = (uint8 *) madt + sizeof (ACPI_TABLE_MADT);
      while (ptr < lim) {
        switch (((ACPI_SUBTABLE_HEADER *) ptr)->Type) {
        case ACPI_MADT_TYPE_LOCAL_APIC:{
            /* Processor entry */
            ACPI_MADT_LOCAL_APIC *sub = (ACPI_MADT_LOCAL_APIC *) ptr;
            printf ("Processor: %X APIC-ID: %X %s",
                    sub->ProcessorId,
                    sub->Id,
                    sub->LapicFlags & 1 ? "(enabled)" : "(disabled)");
            if (acpi_add_processor (sub)) {
              printf (" (booted)");
            }
            printf ("\n");
            break;
          }
        case ACPI_MADT_TYPE_IO_APIC:{
            /* IO-APIC entry */
            ACPI_MADT_IO_APIC *sub = (ACPI_MADT_IO_APIC *) ptr;
            printf ("IO-APIC ID: %X Address: %X IRQBase: %X\n",
                    sub->Id, sub->Address, sub->GlobalIrqBase);
            if (mp_num_IOAPICs == MAX_IOAPICS)
              panic ("Too many IO-APICs.");
            mp_IOAPIC_addr = sub->Address;
            mp_IOAPICs[mp_num_IOAPICs].id = sub->Id;
            mp_IOAPICs[mp_num_IOAPICs].address = sub->Address;
            mp_IOAPICs[mp_num_IOAPICs].startGSI = sub->GlobalIrqBase;
            mp_IOAPICs[mp_num_IOAPICs].numGSIs = IOAPIC_num_entries();
            mp_num_IOAPICs++;
            break;
          }
        case ACPI_MADT_TYPE_INTERRUPT_OVERRIDE:{
            /* Interrupt Override entry */
            ACPI_MADT_INTERRUPT_OVERRIDE *sub =
              (ACPI_MADT_INTERRUPT_OVERRIDE *) ptr;
            printf ("Int. Override: Bus: %X SourceIRQ: %X GlobalIRQ: %X\n",
                    sub->Bus, sub->SourceIrq, sub->GlobalIrq);
            if (mp_num_overrides == MAX_INT_OVERRIDES)
              panic ("Too many interrupt overrides.");
            mp_overrides[mp_num_overrides].src_bus = sub->Bus;
            mp_overrides[mp_num_overrides].src_IRQ = sub->SourceIrq;
            mp_overrides[mp_num_overrides].dest_GSI = sub->GlobalIrq;
            mp_num_overrides++;
            break;
          }
        default:
          printf ("MADT sub-entry: %X\n",
                  ((ACPI_SUBTABLE_HEADER *) ptr)->Type);
          break;
        }
        ptr += ((ACPI_SUBTABLE_HEADER *) ptr)->Length;
      }
    } else {
      printf ("AcpiGetTable MADT: FAILED\n");
      return 0;
    }
    if (AcpiGetTable (ACPI_SIG_FADT, 0, (ACPI_TABLE_HEADER **) & fadt) ==
        AE_OK) {
      /* Fixed ACPI Description Table */
      printf ("Bootflags: %s %s %s\n",
              (fadt->BootFlags & ACPI_FADT_LEGACY_DEVICES) ?
              "HAS_LEGACY_DEVICES" : "NO_LEGACY_DEVICES",
              (fadt->BootFlags & ACPI_FADT_8042) ?
              "HAS_KBD_8042" : "NO_KBD_8042",
              (fadt->BootFlags & ACPI_FADT_NO_VGA) ?
              "NO_VGA_PROBING" : "VGA_PROBING_OK");
    } else {
      printf ("AcpiGetTable FADT: FAILED\n");
    }
    if (AcpiGetTable (ACPI_SIG_BOOT, 0, (ACPI_TABLE_HEADER **) & boot) ==
        AE_OK) {
      /* Simple Boot Information Table */
      printf ("BOOT: CmosIndex=%X\n", boot->CmosIndex);
    }
    if (AcpiGetTable (ACPI_SIG_TCPA, 0, (ACPI_TABLE_HEADER **) & tcpa) ==
        AE_OK) {
      /* Trusted Computing Platform Alliance table */
      printf ("TCPA: MaxLog=%X Addr Upper: %.8X Lower: %.8X\n",
              tcpa->MaxLogLength,
              (uint32) (tcpa->LogAddress >> 32), (uint32) tcpa->LogAddress);
    }
    if (AcpiGetTable (ACPI_SIG_HPET, 0, (ACPI_TABLE_HEADER **) & hpet) ==
        AE_OK) {
      /* High Precision Event Timer table */
      printf ("HPET: ID: %X Addr: %p Seq#: %X MinTick: %X Flags: %X\n",
              hpet->Id,
              hpet->Address,
              (uint32) hpet->Sequence,
              (uint32) hpet->MinimumTick, (uint32) hpet->Flags);
    }
    if (AcpiGetTable (ACPI_SIG_MCFG, 0, (ACPI_TABLE_HEADER **) & mcfg) ==
        AE_OK) {
      /* PCI Memory Mapped Configuration table */
      printf ("MCFG: Length: %X Reserved: %.8X%.8X\n",
              mcfg->Header.Length,
              *((uint32 *) mcfg->Reserved),
              *((uint32 *) (mcfg->Reserved + 4)));
    }
    if (AcpiGetTable (ACPI_SIG_ASF, 0, (ACPI_TABLE_HEADER **) & asf) == AE_OK) {
      /* Alert Standard Format table */
      printf ("ASF: Length: %X\n", mcfg->Header.Length);
    }
  } else
    return 0;

  return mp_num_cpus;
}

#undef printf

/* A small wrapper around smp_boot_cpu() which does some checks and
 * maintains two small tables. */
static int
acpi_add_processor (ACPI_MADT_LOCAL_APIC * ptr)
{
  uint8 this_apic_id = LAPIC_get_physical_ID ();
  uint8 apic_id = ptr->Id;

  if (!(ptr->LapicFlags & 1))
    return 0;                   /* disabled processor */
  if (this_apic_id == apic_id)
    return 0;                   /* bootstrap processor */

  if (smp_boot_cpu (apic_id, APIC_VER_NEW)) {
    CPU_to_APIC[mp_num_cpus] = apic_id;
    APIC_to_CPU[apic_id] = mp_num_cpus;
    mp_num_cpus++;
    return 1;
  } else
    return 0;
}

/* ************************************************** */

extern char const *AcpiGbl_ExceptionNames_Env[];

ACPI_STATUS
DisplayOneDevice (ACPI_HANDLE ObjHandle, UINT32 Level, void *Context,
                  void **RetVal)
{
  ACPI_STATUS Status;
  ACPI_DEVICE_INFO *Info;
  ACPI_BUFFER Path;
  ACPI_BUFFER Result;
  ACPI_OBJECT Obj;
  char Buffer[256];

  Path.Length = sizeof (Buffer);
  Path.Pointer = Buffer;

  Status = AcpiGetName (ObjHandle, ACPI_FULL_PATHNAME, &Path);
  if (ACPI_SUCCESS (Status)) {
    com1_printf ("%s: \n", Path.Pointer);
  }
  Status = AcpiGetObjectInfo (ObjHandle, &Info);
  if (ACPI_SUCCESS (Status)) {
    com1_printf ("    ");
    if (Info->Valid & ACPI_VALID_STA)
      com1_printf (" STA %.8X", Info->CurrentStatus);
    if (Info->Valid & ACPI_VALID_ADR)
      com1_printf (" ADR %.8X", Info->Address);
    if (Info->Valid & ACPI_VALID_HID)
      com1_printf (" HID %s", Info->HardwareId.String);
    if (Info->Valid & ACPI_VALID_UID)
      com1_printf (" UID %s", Info->UniqueId.String);
    if (Info->Valid & ACPI_VALID_CID)
      com1_printf (" CID");

    ACPI_FREE (Info);
  }

  Result.Length = sizeof (Obj);
  Result.Pointer = &Obj;
  Status =
    AcpiEvaluateObjectTyped (ObjHandle, "_DDN", NULL, &Result,
                             ACPI_TYPE_STRING);
  if (ACPI_SUCCESS (Status)) {
    com1_printf (" DDN=%s", Obj.String.Pointer);
  }

  Result.Length = sizeof (Obj);
  Result.Pointer = &Obj;
  Status =
    AcpiEvaluateObjectTyped (ObjHandle, "_STR", NULL, &Result,
                             ACPI_TYPE_STRING);
  if (ACPI_SUCCESS (Status)) {
    com1_printf (" STR=%s", Obj.String.Pointer);
  }

  Result.Length = sizeof (Obj);
  Result.Pointer = &Obj;
  Status =
    AcpiEvaluateObjectTyped (ObjHandle, "_MLS", NULL, &Result,
                             ACPI_TYPE_STRING);
  if (ACPI_SUCCESS (Status)) {
    com1_printf (" MLS=%s", Obj.String.Pointer);
  }

  com1_printf ("\n");


  return AE_OK;
}



/* End ACPI support */
