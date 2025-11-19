/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
//  Copyright (C) 2025  The Bochs Project
//
//  This library is free software; you can redistribute it and/or
//  modify it under the terms of the GNU Lesser General Public
//  License as published by the Free Software Foundation; either
//  version 2 of the License, or (at your option) any later version.
//
//  This library is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
//  Lesser General Public License for more details.
//
//  You should have received a copy of the GNU Lesser General Public
//  License along with this library; if not, write to the Free Software
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

// QEMU fw_cfg device - Firmware Configuration Interface
// Provides platform configuration data to firmware (OVMF/UEFI)

#define BX_PLUGGABLE

#include "iodev.h"

#if BX_SUPPORT_PCI

#include "fwcfg.h"
#include <vector>

// Include precompiled DSDT (AML bytecode)
#include "../bios/acpi-dsdt.hex"

#define LOG_THIS theFwCfg->

bx_fwcfg_c *theFwCfg = NULL;

// Plugin entry point
extern "C" {
PLUGIN_ENTRY_FOR_MODULE(fwcfg)
{
  if (mode == PLUGIN_INIT) {
    theFwCfg = new bx_fwcfg_c();
    BX_REGISTER_DEVICE_DEVMODEL(plugin, type, theFwCfg, BX_PLUGIN_FWCFG);
  } else if (mode == PLUGIN_FINI) {
    delete theFwCfg;
  } else if (mode == PLUGIN_PROBE) {
    return (int)PLUGTYPE_OPTIONAL;
  }
  return 0; // Success
}
}

// Constructor
bx_fwcfg_c::bx_fwcfg_c()
{
  put("FWCFG");
  memset(&s, 0, sizeof(s));
}

// Destructor
bx_fwcfg_c::~bx_fwcfg_c()
{
  cleanup_file_directory();

  // Free file data
  for (size_t i = 0; i < s.files.size(); i++) {
    if (s.files[i].data != NULL) {
      delete[] s.files[i].data;
    }
  }
  s.files.clear();

  SIM->get_bochs_root()->remove("fwcfg");
  BX_DEBUG(("Exit"));
}

// Initialize device
void bx_fwcfg_c::init(void)
{
  BX_INFO(("QEMU fw_cfg device"));

  // Register I/O ports
  DEV_register_iowrite_handler(this, write_handler, FW_CFG_PORT_SEL, "fw_cfg selector", 3);
  DEV_register_ioread_handler(this, read_handler, FW_CFG_PORT_DATA, "fw_cfg data", 1);
  DEV_register_iowrite_handler(this, write_handler, FW_CFG_PORT_DATA, "fw_cfg data", 1);

  // Initialize signature
  s.signature[0] = 'Q';
  s.signature[1] = 'E';
  s.signature[2] = 'M';
  s.signature[3] = 'U';

  // Interface version (1 = supports files, 3 = files + DMA)
  // Note: DMA is not implemented, so we only advertise file support
  s.interface_version = 0x00000001;

  // Get RAM size from memory subsystem
  s.ram_size = (Bit64u)SIM->get_param_num(BXPN_MEM_SIZE)->get() * BX_CONST64(1024) * BX_CONST64(1024);

  // Get CPU count
  s.nb_cpus = (Bit16u)SIM->get_param_num(BXPN_CPU_NPROCESSORS)->get();
  s.max_cpus = (Bit16u)SIM->get_param_num(BXPN_CPU_NCORES)->get() *
               (Bit16u)SIM->get_param_num(BXPN_CPU_NTHREADS)->get() *
               s.nb_cpus;
  if (s.max_cpus == 0) s.max_cpus = s.nb_cpus;

  // Machine ID (1 = PC)
  s.machine_id = 1;

  // Graphics mode
  s.nographic = 0; // Assume graphics unless nogui

  // Generate random UUID
  for (int i = 0; i < 16; i++) {
    s.uuid[i] = (Bit8u)(rand() & 0xFF);
  }
  // Set UUID version 4 (random)
  s.uuid[6] = (s.uuid[6] & 0x0F) | 0x40;
  s.uuid[8] = (s.uuid[8] & 0x3F) | 0x80;

  // Initialize file directory
  s.next_file_selector = 0x0020; // Files start at selector 32
  s.file_dir_data = NULL;
  s.file_dir_size = 0;

  // Generate E820 memory map
  generate_e820_map();

  // Generate ACPI tables
  generate_acpi_tables();

  // Generate ACPI table-loader (for OVMF)
  generate_acpi_loader();

  // Generate SMBIOS tables
  generate_smbios_tables();

  // Generate bootorder
  generate_bootorder();

  BX_INFO(("fw_cfg initialized: RAM=%llu MB, CPUs=%u/%u",
           (unsigned long long)(s.ram_size / (1024*1024)), s.nb_cpus, s.max_cpus));
}

// Reset device
void bx_fwcfg_c::reset(unsigned type)
{
  s.cur_selector = 0;
  s.cur_offset = 0;
}

// Register state for save/restore
void bx_fwcfg_c::register_state(void)
{
  bx_list_c *list = new bx_list_c(SIM->get_bochs_root(), "fwcfg", "QEMU fw_cfg State");
  BXRS_HEX_PARAM_FIELD(list, cur_selector, s.cur_selector);
  BXRS_HEX_PARAM_FIELD(list, cur_offset, s.cur_offset);
}

// I/O write handler
void bx_fwcfg_c::write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len)
{
  bx_fwcfg_c *class_ptr = (bx_fwcfg_c *)this_ptr;
  class_ptr->write(address, value, io_len);
}

void bx_fwcfg_c::write(Bit32u address, Bit32u value, unsigned io_len)
{
  if (address == FW_CFG_PORT_SEL) {
    // Selector register write (16-bit little-endian)
    Bit16u selector = (Bit16u)(value & 0xFFFF);
    select_item(selector);
    BX_DEBUG(("write selector: 0x%04x", selector));
  } else if (address == FW_CFG_PORT_DATA) {
    // Data register write (8-bit)
    write_byte((Bit8u)(value & 0xFF));
  }
}

// I/O read handler
Bit32u bx_fwcfg_c::read_handler(void *this_ptr, Bit32u address, unsigned io_len)
{
  bx_fwcfg_c *class_ptr = (bx_fwcfg_c *)this_ptr;
  return class_ptr->read(address, io_len);
}

Bit32u bx_fwcfg_c::read(Bit32u address, unsigned io_len)
{
  if (address == FW_CFG_PORT_DATA) {
    // Data register read (8-bit)
    return read_byte();
  }
  return 0xFF;
}

// Select item and reset offset
void bx_fwcfg_c::select_item(Bit16u selector)
{
  s.cur_selector = selector;
  s.cur_offset = 0;

  // Log ACPI-related file selections for verification
  if (selector >= 0x0020) {
    int file_idx = find_file_by_selector(selector);
    if (file_idx >= 0) {
      const char *filename = s.files[file_idx].name;
      if (strstr(filename, "acpi") || strstr(filename, "table-loader")) {
        BX_DEBUG(("OS accessing ACPI file: '%s' (selector=0x%04x, size=%u)",
                  filename, selector, s.files[file_idx].size));
      }
    }
  }
}

// Read one byte from currently selected item
Bit8u bx_fwcfg_c::read_byte()
{
  Bit8u value = 0xFF;
  Bit32u offset = s.cur_offset++;

  // Handle built-in selectors
  if (s.cur_selector < 0x0020) {
    switch (s.cur_selector) {
      case FW_CFG_SIGNATURE:
        if (offset < 4) {
          value = s.signature[offset];
        }
        break;

      case FW_CFG_ID:
        if (offset < 4) {
          value = (s.interface_version >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_UUID:
        if (offset < 16) {
          value = s.uuid[offset];
        }
        break;

      case FW_CFG_RAM_SIZE:
        if (offset < 8) {
          value = (s.ram_size >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_NOGRAPHIC:
        if (offset < 2) {
          value = (s.nographic >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_NB_CPUS:
        if (offset < 2) {
          value = (s.nb_cpus >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_MAX_CPUS:
        if (offset < 2) {
          value = (s.max_cpus >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_MACHINE_ID:
        if (offset < 2) {
          value = (s.machine_id >> (offset * 8)) & 0xFF;
        }
        break;

      case FW_CFG_FILE_DIR:
        // Generate file directory on demand
        if (s.file_dir_data == NULL) {
          BX_INFO(("FW_CFG_FILE_DIR: Generating file directory on first read"));
          generate_file_directory();
          BX_INFO(("FW_CFG_FILE_DIR: Generation complete, dir_size=%u", s.file_dir_size));
        }
        if (offset < s.file_dir_size) {
          value = s.file_dir_data[offset];
          if (offset < 4) {
            BX_INFO(("FW_CFG_FILE_DIR: read offset=%u value=0x%02x (file count byte %u)", offset, value, offset));
          } else if (offset % 64 < 8 || (offset >= 4 && offset < 68)) {
            BX_INFO(("FW_CFG_FILE_DIR: read offset=%u value=0x%02x", offset, value));
          }
        } else {
          BX_INFO(("FW_CFG_FILE_DIR: read offset=%u >= dir_size=%u, returning 0xFF", offset, s.file_dir_size));
        }
        break;

      default:
        // Unknown selector
        if (offset == 0) {
          BX_DEBUG(("read from unknown selector 0x%04x", s.cur_selector));
        }
        value = 0xFF;
        break;
    }
  } else {
    // Handle file-based selectors
    int file_idx = find_file_by_selector(s.cur_selector);
    if (file_idx >= 0) {
      if (offset < s.files[file_idx].size) {
        value = s.files[file_idx].data[offset];

        // Log when ACPI files are fully read
        const char *filename = s.files[file_idx].name;
        if ((strstr(filename, "acpi") || strstr(filename, "table-loader")) &&
            offset == s.files[file_idx].size - 1) {
          BX_DEBUG(("OS finished reading ACPI file: '%s' (%u bytes)",
                    filename, s.files[file_idx].size));
        }
      }
    } else {
      if (offset == 0) {
        BX_DEBUG(("read from unknown file selector 0x%04x", s.cur_selector));
      }
      value = 0xFF;
    }
  }

  return value;
}

// Write one byte to currently selected item
void bx_fwcfg_c::write_byte(Bit8u value)
{
  Bit32u offset = s.cur_offset++;

  // Currently no writable items implemented
  // Future: bootorder file will be writable
  BX_DEBUG(("write to selector 0x%04x offset 0x%x: 0x%02x (ignored)",
            s.cur_selector, offset, value));
}

// Add a file-based item
void bx_fwcfg_c::add_file(const char *name, Bit8u *data, Bit32u size, bool writable)
{
  if (strlen(name) >= 56) {
    BX_ERROR(("fw_cfg: filename too long: %s", name));
    return;
  }

  FWCfgEntry entry;
  entry.selector = s.next_file_selector++;
  strncpy(entry.name, name, 55);
  entry.name[55] = '\0';
  entry.data = data;
  entry.size = size;
  entry.writable = writable;

  s.files.push_back(entry);

  // Invalidate cached file directory
  cleanup_file_directory();

  BX_INFO(("fw_cfg: added file '%s' (selector=0x%04x, size=%u)",
           name, entry.selector, size));
}

// Find file by selector
int bx_fwcfg_c::find_file_by_selector(Bit16u selector)
{
  for (size_t i = 0; i < s.files.size(); i++) {
    if (s.files[i].selector == selector) {
      return (int)i;
    }
  }
  return -1;
}

// Generate file directory (selector 0x0019)
void bx_fwcfg_c::generate_file_directory()
{
  cleanup_file_directory();

  Bit32u num_files = (Bit32u)s.files.size();
  s.file_dir_size = 4 + (num_files * 64); // 4-byte count + 64 bytes per file
  s.file_dir_data = new Bit8u[s.file_dir_size];

  // Write file count (big-endian)
  s.file_dir_data[0] = (num_files >> 24) & 0xFF;
  s.file_dir_data[1] = (num_files >> 16) & 0xFF;
  s.file_dir_data[2] = (num_files >> 8) & 0xFF;
  s.file_dir_data[3] = num_files & 0xFF;

  // Write file entries
  for (Bit32u i = 0; i < num_files; i++) {
    Bit8u *entry = &s.file_dir_data[4 + (i * 64)];
    FWCfgEntry *file = &s.files[i];

    // File size (big-endian)
    entry[0] = (file->size >> 24) & 0xFF;
    entry[1] = (file->size >> 16) & 0xFF;
    entry[2] = (file->size >> 8) & 0xFF;
    entry[3] = file->size & 0xFF;

    // Selector (big-endian)
    entry[4] = (file->selector >> 8) & 0xFF;
    entry[5] = file->selector & 0xFF;

    // Reserved
    entry[6] = 0;
    entry[7] = 0;

    // Name (NUL-padded)
    memset(&entry[8], 0, 56);
    strncpy((char *)&entry[8], file->name, 55);
  }

  BX_INFO(("generated file directory: %u files, %u bytes", num_files, s.file_dir_size));

  // Log details of each file for debugging
  for (Bit32u i = 0; i < num_files; i++) {
    FWCfgEntry *file = &s.files[i];
    BX_INFO(("  File %u: selector=0x%04x size=%u name='%s'", i, file->selector, file->size, file->name));
  }
}

// Clean up file directory cache
void bx_fwcfg_c::cleanup_file_directory()
{
  if (s.file_dir_data != NULL) {
    delete[] s.file_dir_data;
    s.file_dir_data = NULL;
    s.file_dir_size = 0;
  }
}

// ==================================================================
// ACPI Table Generation
// ==================================================================

// Calculate ACPI table checksum
// Returns the value that when added to the sum of all bytes makes the total 0
Bit8u bx_fwcfg_c::acpi_checksum(void *data, Bit32u length)
{
  Bit8u *bytes = (Bit8u *)data;
  Bit32u sum = 0;
  for (Bit32u i = 0; i < length; i++) {
    sum += bytes[i];
  }
  return (Bit8u)((-sum) & 0xFF);
}

// Build standard ACPI table header
void bx_fwcfg_c::acpi_build_table_header(ACPITableHeader *h, const char *sig, Bit32u len, Bit8u rev)
{
  memcpy(h->signature, sig, 4);
  h->length = len;  // Little-endian (x86)
  h->revision = rev;
  memcpy(h->oem_id, "BOCHS ", 6);
  memcpy(h->oem_table_id, "BXPC", 4);
  memcpy(h->oem_table_id + 4, sig, 4);
  h->oem_revision = 1;
  memcpy(h->asl_compiler_id, "BXPC", 4);
  h->asl_compiler_revision = 1;
  h->checksum = 0;  // Will be calculated after table is complete
}

// Generate ACPI tables and expose via fw_cfg
void bx_fwcfg_c::generate_acpi_tables()
{
  BX_INFO(("Generating ACPI tables for UEFI/OVMF"));

  // Calculate total size needed for all tables
  Bit32u rsdp_size = sizeof(ACPIRSDP);
  Bit32u rsdt_size = sizeof(ACPIRSTD);
  Bit32u fadt_size = sizeof(ACPIFADT);
  Bit32u facs_size = sizeof(ACPIFACS);
  Bit32u dsdt_size = sizeof(AmlCode);
  Bit32u madt_size = sizeof(ACPIMADT) +
                     s.nb_cpus * sizeof(MADTProcessorAPIC) +
                     sizeof(MADTIOAPIC) +
                     sizeof(MADTIRQOverride);

  // Align FACS to 64-byte boundary
  Bit32u base_offset = 0;
  Bit32u rsdt_offset = base_offset;
  Bit32u fadt_offset = rsdt_offset + rsdt_size;
  Bit32u facs_offset = (fadt_offset + fadt_size + 63) & ~63;  // 64-byte aligned
  Bit32u dsdt_offset = facs_offset + facs_size;
  Bit32u madt_offset = (dsdt_offset + dsdt_size + 7) & ~7;  // 8-byte aligned
  Bit32u tables_size = madt_offset + madt_size;

  BX_DEBUG(("ACPI table offsets: RSDT=0x%x FADT=0x%x FACS=0x%x DSDT=0x%x MADT=0x%x total=%u",
           rsdt_offset, fadt_offset, facs_offset, dsdt_offset, madt_offset, tables_size));

  // Allocate buffer for all tables (except RSDP which goes in separate file)
  Bit8u *tables = new Bit8u[tables_size];
  memset(tables, 0, tables_size);

  // Build RSDT (Root System Description Table)
  ACPIRSTD *rsdt = (ACPIRSTD *)(tables + rsdt_offset);
  acpi_build_table_header(&rsdt->header, "RSDT", rsdt_size, 1);
  rsdt->entry[0] = fadt_offset;  // Pointer to FADT
  rsdt->entry[1] = madt_offset;  // Pointer to MADT
  rsdt->entry[2] = 0;            // Unused
  rsdt->entry[3] = 0;            // Unused
  rsdt->header.checksum = acpi_checksum(rsdt, rsdt_size);

  // Build FADT (Fixed ACPI Description Table)
  ACPIFADT *fadt = (ACPIFADT *)(tables + fadt_offset);
  acpi_build_table_header(&fadt->header, "FACP", fadt_size, 1);
  fadt->firmware_ctrl = facs_offset;   // Pointer to FACS
  fadt->dsdt = dsdt_offset;             // Pointer to DSDT
  fadt->model = 1;                      // PC/AT compatible
  fadt->reserved1 = 0;
  fadt->sci_int = 9;                    // SCI interrupt (IRQ 9)
  fadt->smi_cmd = 0xB2;                 // SMI command port
  fadt->acpi_enable = 0xF1;             // Value to enable ACPI
  fadt->acpi_disable = 0xF0;            // Value to disable ACPI
  fadt->S4bios_req = 0;
  fadt->reserved2 = 0;
  fadt->pm1a_evt_blk = 0x0600;          // PM1a event block (Bochs PIIX3)
  fadt->pm1b_evt_blk = 0;               // No PM1b
  fadt->pm1a_cnt_blk = 0x0604;          // PM1a control block
  fadt->pm1b_cnt_blk = 0;               // No PM1b
  fadt->pm2_cnt_blk = 0;                // No PM2
  fadt->pm_tmr_blk = 0x0608;            // PM timer block
  fadt->gpe0_blk = 0x0620;              // GPE0 block
  fadt->gpe1_blk = 0;                   // No GPE1
  fadt->pm1_evt_len = 4;                // 4 bytes
  fadt->pm1_cnt_len = 2;                // 2 bytes
  fadt->pm2_cnt_len = 0;
  fadt->pm_tmr_len = 4;                 // 4 bytes
  fadt->gpe0_blk_len = 4;               // 4 bytes
  fadt->gpe1_blk_len = 0;
  fadt->gpe1_base = 0;
  fadt->reserved3 = 0;
  fadt->plvl2_lat = 0xFFFF;             // C2 not supported
  fadt->plvl3_lat = 0xFFFF;             // C3 not supported
  fadt->flush_size = 0;
  fadt->flush_stride = 0;
  fadt->duty_offset = 0;
  fadt->duty_width = 0;
  fadt->day_alrm = 0;
  fadt->mon_alrm = 0;
  fadt->century = 0;
  fadt->reserved4[0] = 0;
  fadt->reserved4[1] = 0;
  fadt->reserved4[2] = 0;
  // Flags: WBINVD + PROC_C1 + PWR_BUTTON + SLP_BUTTON + FIX_RTC
  fadt->flags = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 6);
  fadt->header.checksum = acpi_checksum(fadt, fadt_size);

  // Build FACS (Firmware ACPI Control Structure)
  ACPIFACS *facs = (ACPIFACS *)(tables + facs_offset);
  memcpy(facs->signature, "FACS", 4);
  facs->length = facs_size;
  facs->hardware_signature = 0;
  facs->firmware_waking_vector = 0;
  facs->global_lock = 0;
  facs->flags = 0;
  memset(facs->reserved, 0, sizeof(facs->reserved));
  // FACS does not have a checksum field

  // Copy DSDT (precompiled AML bytecode)
  Bit8u *dsdt = tables + dsdt_offset;
  memcpy(dsdt, AmlCode, dsdt_size);

  // Build MADT (Multiple APIC Description Table)
  ACPIMADT *madt = (ACPIMADT *)(tables + madt_offset);
  acpi_build_table_header(&madt->header, "APIC", madt_size, 1);
  madt->local_apic_address = ACPI_LAPIC_ADDRESS;
  madt->flags = 1;  // PC-AT compatible (dual 8259 PICs)

  // Add Processor Local APIC entries (one per CPU)
  Bit8u *madt_ptr = (Bit8u *)(madt + 1);
  for (Bit16u i = 0; i < s.nb_cpus; i++) {
    MADTProcessorAPIC *apic = (MADTProcessorAPIC *)madt_ptr;
    apic->type = ACPI_MADT_TYPE_LOCAL_APIC;
    apic->length = sizeof(MADTProcessorAPIC);
    apic->processor_id = i;
    apic->apic_id = i;
    apic->flags = 1;  // Processor enabled
    madt_ptr += sizeof(MADTProcessorAPIC);
  }

  // Add I/O APIC entry
  MADTIOAPIC *io_apic = (MADTIOAPIC *)madt_ptr;
  io_apic->type = ACPI_MADT_TYPE_IO_APIC;
  io_apic->length = sizeof(MADTIOAPIC);
  io_apic->io_apic_id = s.nb_cpus;
  io_apic->reserved = 0;
  io_apic->io_apic_address = ACPI_IOAPIC_ADDRESS;
  io_apic->global_irq_base = 0;
  madt_ptr += sizeof(MADTIOAPIC);

  // Add Interrupt Source Override (IRQ 0 -> GSI 2)
  MADTIRQOverride *irq_override = (MADTIRQOverride *)madt_ptr;
  irq_override->type = ACPI_MADT_TYPE_IRQ_OVERRIDE;
  irq_override->length = sizeof(MADTIRQOverride);
  irq_override->bus = 0;       // ISA bus
  irq_override->source = 0;    // IRQ 0
  irq_override->gsi = 2;       // GSI 2
  irq_override->flags = 0;     // Default flags

  madt->header.checksum = acpi_checksum(madt, madt_size);

  // Build RSDP (Root System Description Pointer)
  // This goes in a separate file
  Bit8u *rsdp_data = new Bit8u[rsdp_size];
  ACPIRSDP *rsdp = (ACPIRSDP *)rsdp_data;
  memset(rsdp, 0, rsdp_size);
  memcpy(rsdp->signature, "RSD PTR ", 8);  // Note the space at the end
  memcpy(rsdp->oem_id, "BOCHS ", 6);
  rsdp->revision = 0;  // ACPI 1.0
  rsdp->rsdt_physical_address = rsdt_offset;  // Offset in tables blob
  rsdp->length = 0;
  rsdp->xsdt_physical_address = 0;
  rsdp->extended_checksum = 0;
  memset(rsdp->reserved, 0, sizeof(rsdp->reserved));
  rsdp->checksum = acpi_checksum(rsdp, 20);  // Checksum first 20 bytes only

  // Expose ACPI tables via fw_cfg
  add_file("etc/acpi/rsdp", rsdp_data, rsdp_size, false);
  add_file("etc/acpi/tables", tables, tables_size, false);

  BX_INFO(("ACPI tables generated: RSDP=%u bytes, tables=%u bytes (RSDT+FADT+FACS+DSDT+MADT)",
           rsdp_size, tables_size));
  BX_INFO(("  RSDT @ 0x%x (%u bytes)", rsdt_offset, rsdt_size));
  BX_INFO(("  FADT @ 0x%x (%u bytes)", fadt_offset, fadt_size));
  BX_INFO(("  FACS @ 0x%x (%u bytes)", facs_offset, facs_size));
  BX_INFO(("  DSDT @ 0x%x (%u bytes)", dsdt_offset, dsdt_size));
  BX_INFO(("  MADT @ 0x%x (%u bytes, %u CPUs)", madt_offset, madt_size, s.nb_cpus));
}

// ==================================================================
// ACPI Table Loader Generation (etc/table-loader)
// ==================================================================

// Generate ACPI table-loader commands for OVMF
// This creates the etc/table-loader file that tells OVMF how to:
// - Allocate memory for ACPI tables
// - Patch pointers between tables
// - Calculate checksums
void bx_fwcfg_c::generate_acpi_loader()
{
  BX_INFO(("Generating ACPI table-loader for OVMF"));

  // Calculate number of commands needed:
  // - 2 ALLOCATE commands (for etc/acpi/rsdp and etc/acpi/tables)
  // - 1 ADD_POINTER command (RSDP -> RSDT)
  // - 2 ADD_CHECKSUM commands (RSDP and all tables)
  const int num_commands = 5;
  const Bit32u loader_size = num_commands * sizeof(BiosLinkerLoaderEntry);

  Bit8u *loader_data = new Bit8u[loader_size];
  memset(loader_data, 0, loader_size);

  BiosLinkerLoaderEntry *cmd = (BiosLinkerLoaderEntry *)loader_data;
  int cmd_idx = 0;

  // Command 1: ALLOCATE etc/acpi/tables in high memory with 4K alignment
  BX_DEBUG(("Linker command %d: ALLOCATE etc/acpi/tables", cmd_idx));
  cmd[cmd_idx].command = BIOS_LINKER_LOADER_COMMAND_ALLOCATE;
  strncpy(cmd[cmd_idx].alloc.file, "etc/acpi/tables", BIOS_LINKER_LOADER_FILESZ - 1);
  cmd[cmd_idx].alloc.align = 4096;  // 4K alignment for ACPI tables
  cmd[cmd_idx].alloc.zone = BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH;
  cmd_idx++;

  // Command 2: ADD_CHECKSUM for all tables in etc/acpi/tables
  // Note: Individual table checksums are already calculated in generate_acpi_tables()
  // This command is for the overall tables blob if needed
  BX_DEBUG(("Linker command %d: ADD_CHECKSUM etc/acpi/tables (skip - checksums pre-calculated)", cmd_idx));
  // Skip this - checksums already done

  // Command 3: ALLOCATE etc/acpi/rsdp in FSEG (0xF segment for legacy compatibility)
  BX_DEBUG(("Linker command %d: ALLOCATE etc/acpi/rsdp", cmd_idx));
  cmd[cmd_idx].command = BIOS_LINKER_LOADER_COMMAND_ALLOCATE;
  strncpy(cmd[cmd_idx].alloc.file, "etc/acpi/rsdp", BIOS_LINKER_LOADER_FILESZ - 1);
  cmd[cmd_idx].alloc.align = 16;  // 16-byte alignment for RSDP
  cmd[cmd_idx].alloc.zone = BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG;
  cmd_idx++;

  // Command 4: ADD_POINTER from RSDP to RSDT
  // The RSDP contains rsdt_physical_address field at offset 16
  // This needs to point to the RSDT table in etc/acpi/tables
  BX_DEBUG(("Linker command %d: ADD_POINTER RSDP->RSDT", cmd_idx));
  cmd[cmd_idx].command = BIOS_LINKER_LOADER_COMMAND_ADD_POINTER;
  strncpy(cmd[cmd_idx].pointer.dest_file, "etc/acpi/rsdp", BIOS_LINKER_LOADER_FILESZ - 1);
  strncpy(cmd[cmd_idx].pointer.src_file, "etc/acpi/tables", BIOS_LINKER_LOADER_FILESZ - 1);
  cmd[cmd_idx].pointer.offset = 16;  // Offset of rsdt_physical_address in RSDP
  cmd[cmd_idx].pointer.size = 4;     // 32-bit pointer
  cmd_idx++;

  // Command 5: ADD_CHECKSUM for RSDP (first 20 bytes)
  BX_DEBUG(("Linker command %d: ADD_CHECKSUM RSDP", cmd_idx));
  cmd[cmd_idx].command = BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM;
  strncpy(cmd[cmd_idx].cksum.file, "etc/acpi/rsdp", BIOS_LINKER_LOADER_FILESZ - 1);
  cmd[cmd_idx].cksum.offset = 8;   // Offset of checksum field in RSDP
  cmd[cmd_idx].cksum.start = 0;    // Start of checksummed region
  cmd[cmd_idx].cksum.length = 20;  // ACPI 1.0 RSDP is 20 bytes
  cmd_idx++;

  BX_INFO(("Generated %d table-loader commands (%u bytes)", cmd_idx, loader_size));

  // Expose the loader via fw_cfg
  add_file("etc/table-loader", loader_data, loader_size, false);

  BX_INFO(("ACPI table-loader ready for OVMF"));
}

// ==================================================================
// SMBIOS Table Generation
// ==================================================================

// Calculate SMBIOS checksum
Bit8u bx_fwcfg_c::smbios_checksum(void *data, Bit32u length)
{
  Bit8u *bytes = (Bit8u *)data;
  Bit32u sum = 0;
  for (Bit32u i = 0; i < length; i++) {
    sum += bytes[i];
  }
  return (Bit8u)((-sum) & 0xFF);
}

// Helper to add string to SMBIOS structure
static Bit8u* smbios_add_string(Bit8u *p, const char *str)
{
  strcpy((char *)p, str);
  return p + strlen(str) + 1;
}

// Helper to terminate SMBIOS structure strings
static Bit8u* smbios_terminate_strings(Bit8u *p)
{
  // SMBIOS structures end with double-null (empty string after last string)
  *p++ = 0;
  return p;
}

// Generate SMBIOS tables and expose via fw_cfg
void bx_fwcfg_c::generate_smbios_tables()
{
  BX_INFO(("Generating SMBIOS tables for UEFI/OVMF"));

  // Calculate memory size in MB
  Bit32u memsize_mb = (Bit32u)(s.ram_size / (1024 * 1024));

  // Calculate number of memory devices (each covers up to 16GB)
  Bit32u nr_mem_devs = (memsize_mb + 0x3FFF) >> 14;
  if (nr_mem_devs == 0) nr_mem_devs = 1;

  // Estimate buffer size: entry point + all structures + strings
  // Generous estimate to avoid overflow
  Bit32u est_size = sizeof(SMBIOSEntryPoint) + 2048;
  Bit8u *tables = new Bit8u[est_size];
  memset(tables, 0, est_size);

  // Reserve space for entry point (will fill in later)
  Bit8u *p = tables + sizeof(SMBIOSEntryPoint);
  Bit8u *structure_start = p;

  Bit16u nr_structs = 0;
  Bit16u max_struct_size = 0;

  // Helper macro to track structure stats
  #define ADD_SMBIOS_STRUCT(start, end) do { \
    Bit16u struct_size = (end) - (start); \
    if (struct_size > max_struct_size) max_struct_size = struct_size; \
    nr_structs++; \
    p = (end); \
  } while(0)

  // Type 0: BIOS Information
  {
    Bit8u *start = p;
    SMBIOSType0 *t = (SMBIOSType0 *)p;
    t->header.type = 0;
    t->header.length = sizeof(SMBIOSType0);
    t->header.handle = 0x0000;
    t->vendor_str = 1;
    t->bios_version_str = 2;
    t->bios_starting_address_segment = 0xE000;
    t->bios_release_date_str = 3;
    t->bios_rom_size = 0; // (size-1)/64K, 0 for 64K ROM
    t->bios_characteristics = 0x08; // BIOS characteristics not fully populated
    t->bios_characteristics_ext_bytes[0] = 0;
    t->bios_characteristics_ext_bytes[1] = 0;
    t->system_bios_major_release = 1;
    t->system_bios_minor_release = 0;
    t->embedded_controller_major_release = 0xFF;
    t->embedded_controller_minor_release = 0xFF;

    p += sizeof(SMBIOSType0);
    p = smbios_add_string(p, "Bochs");
    p = smbios_add_string(p, "Bochs");
    p = smbios_add_string(p, "11/17/2025");
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 1: System Information
  {
    Bit8u *start = p;
    SMBIOSType1 *t = (SMBIOSType1 *)p;
    t->header.type = 1;
    t->header.length = sizeof(SMBIOSType1);
    t->header.handle = 0x0100;
    t->manufacturer_str = 1;
    t->product_name_str = 2;
    t->version_str = 3;
    t->serial_number_str = 4;
    memcpy(t->uuid, s.uuid, 16); // Use same UUID as fw_cfg
    t->wake_up_type = 6; // Power switch
    t->sku_number_str = 0;
    t->family_str = 0;

    p += sizeof(SMBIOSType1);
    p = smbios_add_string(p, "Bochs");
    p = smbios_add_string(p, "Standard PC (Q35 + ICH9, 2009)");
    p = smbios_add_string(p, "pc-q35-2.4");
    p = smbios_add_string(p, "Not Specified");
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 3: System Enclosure
  {
    Bit8u *start = p;
    SMBIOSType3 *t = (SMBIOSType3 *)p;
    t->header.type = 3;
    t->header.length = sizeof(SMBIOSType3);
    t->header.handle = 0x0300;
    t->manufacturer_str = 1;
    t->type = 1; // Other
    t->version_str = 0;
    t->serial_number_str = 0;
    t->asset_tag_number_str = 0;
    t->boot_up_state = 3; // Safe
    t->power_supply_state = 3; // Safe
    t->thermal_state = 3; // Safe
    t->security_status = 0; // Unknown
    t->oem_defined = 0;
    t->height = 0;
    t->number_of_power_cords = 0;
    t->contained_element_count = 0;

    p += sizeof(SMBIOSType3);
    p = smbios_add_string(p, "Bochs");
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 4: Processor Information (one per CPU)
  for (Bit16u cpu_num = 0; cpu_num < s.nb_cpus; cpu_num++) {
    Bit8u *start = p;
    SMBIOSType4 *t = (SMBIOSType4 *)p;
    t->header.type = 4;
    t->header.length = sizeof(SMBIOSType4);
    t->header.handle = 0x0400 + cpu_num;
    t->socket_designation_str = 1;
    t->processor_type = 3; // Central Processor
    t->processor_family = 1; // Other
    t->processor_manufacturer_str = 2;
    t->processor_id[0] = 0;
    t->processor_id[1] = 0;
    t->processor_version_str = 0;
    t->voltage = 0;
    t->external_clock = 0;
    t->max_speed = 2000; // 2000 MHz
    t->current_speed = 2000;
    t->status = 0x41; // CPU enabled, populated
    t->processor_upgrade = 1; // Other

    p += sizeof(SMBIOSType4);
    p = smbios_add_string(p, "CPU 0");
    p = smbios_add_string(p, "Bochs");
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 16: Physical Memory Array
  {
    Bit8u *start = p;
    SMBIOSType16 *t = (SMBIOSType16 *)p;
    t->header.type = 16;
    t->header.length = sizeof(SMBIOSType16);
    t->header.handle = 0x1000;
    t->location = 3; // System board or motherboard
    t->use = 3; // System memory
    t->error_correction = 3; // None
    t->maximum_capacity = memsize_mb * 1024; // In KB
    t->memory_error_information_handle = 0xFFFE; // Not provided
    t->number_of_memory_devices = nr_mem_devs;

    p += sizeof(SMBIOSType16);
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 17, 19, 20: Memory Device info (one set per device)
  for (Bit32u i = 0; i < nr_mem_devs; i++) {
    Bit32u dev_memsize_mb = ((i == (nr_mem_devs - 1))
                             ? (((memsize_mb - 1) & 0x3FFF) + 1) : 0x4000);

    // Type 17: Memory Device
    {
      Bit8u *start = p;
      SMBIOSType17 *t = (SMBIOSType17 *)p;
      t->header.type = 17;
      t->header.length = sizeof(SMBIOSType17);
      t->header.handle = 0x1100 + i;
      t->physical_memory_array_handle = 0x1000;
      t->memory_error_information_handle = 0xFFFE;
      t->total_width = 64;
      t->data_width = 64;
      t->size = dev_memsize_mb; // Size in MB
      t->form_factor = 9; // DIMM
      t->device_set = 0;
      t->device_locator_str = 1;
      t->bank_locator_str = 0;
      t->memory_type = 7; // SDRAM
      t->type_detail = 0;

      p += sizeof(SMBIOSType17);
      p = smbios_add_string(p, "DIMM 0");
      p = smbios_terminate_strings(p);
      ADD_SMBIOS_STRUCT(start, p);
    }

    // Type 19: Memory Array Mapped Address
    {
      Bit8u *start = p;
      SMBIOSType19 *t = (SMBIOSType19 *)p;
      t->header.type = 19;
      t->header.length = sizeof(SMBIOSType19);
      t->header.handle = 0x1300 + i;
      t->starting_address = i << 24; // In KB
      t->ending_address = t->starting_address + (dev_memsize_mb << 10) - 1;
      t->memory_array_handle = 0x1000;
      t->partition_width = 1;

      p += sizeof(SMBIOSType19);
      p = smbios_terminate_strings(p);
      ADD_SMBIOS_STRUCT(start, p);
    }

    // Type 20: Memory Device Mapped Address
    {
      Bit8u *start = p;
      SMBIOSType20 *t = (SMBIOSType20 *)p;
      t->header.type = 20;
      t->header.length = sizeof(SMBIOSType20);
      t->header.handle = 0x1400 + i;
      t->starting_address = i << 24; // In KB
      t->ending_address = t->starting_address + (dev_memsize_mb << 10) - 1;
      t->memory_device_handle = 0x1100 + i;
      t->memory_array_mapped_address_handle = 0x1300 + i;
      t->partition_row_position = 1;
      t->interleave_position = 0;
      t->interleaved_data_depth = 0;

      p += sizeof(SMBIOSType20);
      p = smbios_terminate_strings(p);
      ADD_SMBIOS_STRUCT(start, p);
    }
  }

  // Type 32: System Boot Information
  {
    Bit8u *start = p;
    SMBIOSType32 *t = (SMBIOSType32 *)p;
    t->header.type = 32;
    t->header.length = sizeof(SMBIOSType32);
    t->header.handle = 0x2000;
    memset(t->reserved, 0, 6);
    t->boot_status = 0; // No errors detected

    p += sizeof(SMBIOSType32);
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  // Type 127: End-of-Table
  {
    Bit8u *start = p;
    SMBIOSType127 *t = (SMBIOSType127 *)p;
    t->header.type = 127;
    t->header.length = sizeof(SMBIOSType127);
    t->header.handle = 0x7F00;

    p += sizeof(SMBIOSType127);
    p = smbios_terminate_strings(p);
    ADD_SMBIOS_STRUCT(start, p);
  }

  #undef ADD_SMBIOS_STRUCT

  // Calculate structure table size
  Bit16u structure_table_length = p - structure_start;

  // Build SMBIOS Entry Point
  SMBIOSEntryPoint *ep = (SMBIOSEntryPoint *)tables;
  memcpy(ep->anchor_string, "_SM_", 4);
  ep->checksum = 0; // Will calculate later
  ep->length = sizeof(SMBIOSEntryPoint);
  ep->smbios_major_version = 2;
  ep->smbios_minor_version = 4;
  ep->max_structure_size = max_struct_size;
  ep->entry_point_revision = 0;
  memset(ep->formatted_area, 0, 5);
  memcpy(ep->intermediate_anchor, "_DMI_", 5);
  ep->intermediate_checksum = 0; // Will calculate later
  ep->structure_table_length = structure_table_length;
  ep->structure_table_address = 0; // Offset 0 in tables (after entry point)
  ep->number_of_structures = nr_structs;
  ep->smbios_bcd_revision = 0x24; // SMBIOS 2.4

  // Calculate checksums
  ep->intermediate_checksum = smbios_checksum((Bit8u *)ep + 0x10, 15);
  ep->checksum = smbios_checksum(ep, ep->length);

  // Calculate total size
  Bit32u total_size = sizeof(SMBIOSEntryPoint) + structure_table_length;

  // Expose SMBIOS tables via fw_cfg (entry point + structures concatenated)
  add_file("etc/smbios/smbios-tables", tables, total_size, false);

  BX_INFO(("SMBIOS tables generated: %u bytes (%u structures, max size=%u)",
           total_size, nr_structs, max_struct_size));
  BX_INFO(("  Entry Point: %u bytes", (Bit32u)sizeof(SMBIOSEntryPoint)));
  BX_INFO(("  Structure Table: %u bytes", structure_table_length));
  BX_INFO(("  Memory: %u MB (%u devices)", memsize_mb, nr_mem_devs));
}

// ==================================================================
// Boot Order Generation for UEFI/OVMF
// ==================================================================

// Generate boot order configuration
// This tells OVMF which devices to boot from and in what order
// Uses OpenFirmware device path notation (same as QEMU)
void bx_fwcfg_c::generate_bootorder()
{
  BX_INFO(("Generating boot order for UEFI/OVMF"));

  // Build bootorder string (newline-separated OpenFirmware device paths)
  // For IDE disk on ata0-master (PIIX3 IDE at PCI 0:1.1):
  //   /pci@i0cf8/ide@1,1/drive@0/disk@0
  //
  // Format breakdown:
  //   pci@i0cf8    - PCI bus (i0cf8 is the PCI config I/O port)
  //   ide@1,1      - IDE controller at PCI device 1, function 1 (PIIX3)
  //   drive@0      - Drive 0 (ata0-master)
  //   disk@0       - Disk 0 (the actual disk, not CD-ROM)

  const char *bootorder_str = "/pci@i0cf8/ide@1,1/drive@0/disk@0\n";
  Bit32u bootorder_len = strlen(bootorder_str);

  // Allocate and copy bootorder string
  Bit8u *bootorder = new Bit8u[bootorder_len];
  memcpy(bootorder, bootorder_str, bootorder_len);

  // Expose bootorder via fw_cfg
  add_file("bootorder", bootorder, bootorder_len, false);

  BX_INFO(("Boot order configured: %u bytes", bootorder_len));
  BX_INFO(("  Primary boot device: /pci@i0cf8/ide@1,1/drive@0/disk@0"));

  // Add boot configuration files
  // etc/boot-fail-wait: Time in seconds to wait after boot failure
  // Set to 5 seconds so OVMF will timeout and show error instead of hanging
  const char *boot_fail_wait = "5";
  Bit32u boot_fail_wait_len = strlen(boot_fail_wait) + 1; // Include null terminator
  Bit8u *boot_fail_wait_data = new Bit8u[boot_fail_wait_len];
  memcpy(boot_fail_wait_data, boot_fail_wait, boot_fail_wait_len);
  add_file("etc/boot-fail-wait", boot_fail_wait_data, boot_fail_wait_len, false);

  // etc/show-boot-menu: 0 = don't show menu, just boot
  Bit8u *show_boot_menu = new Bit8u[2];
  show_boot_menu[0] = '0';
  show_boot_menu[1] = '\0';
  add_file("etc/show-boot-menu", show_boot_menu, 2, false);

  BX_INFO(("Boot configuration: boot-fail-wait=5s, show-boot-menu=0"));
}

// Generate E820 memory map and add to fw_cfg
void bx_fwcfg_c::generate_e820_map()
{
  std::vector<E820Entry> entries;
  E820Entry entry;

  // Get memory size in bytes
  Bit64u ram_size_bytes = s.ram_size;
  Bit64u ram_size_mb = ram_size_bytes / (1024 * 1024);

  BX_INFO(("Generating E820 memory map for %llu MB RAM", (unsigned long long)ram_size_mb));

  // Entry 1: Low memory (0 - 640KB)
  // This is always usable RAM
  entry.address = 0x00000000;
  entry.length = 0x000A0000;  // 640KB
  entry.type = E820_RAM;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu KB) - Usable RAM",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / 1024)));

  // Entry 2: VGA/BIOS area (640KB - 1MB)
  // Reserved for video memory and option ROMs
  entry.address = 0x000A0000;
  entry.length = 0x00060000;  // 384KB (640KB to 1MB)
  entry.type = E820_RESERVED;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu KB) - Reserved (VGA/BIOS)",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / 1024)));

  // Entry 3: High memory (1MB to end of RAM or 3.5GB, whichever is lower)
  // For systems with >3.5GB RAM, we need to split around the UEFI ROM area
  Bit64u high_mem_start = 0x00100000;  // 1MB
  Bit64u high_mem_end = ram_size_bytes;
  Bit64u uefi_rom_start = 0xFFC00000;  // 4GB - 4MB

  if (high_mem_end > uefi_rom_start) {
    // RAM extends into UEFI ROM area, split it
    entry.address = high_mem_start;
    entry.length = uefi_rom_start - high_mem_start;
    entry.type = E820_RAM;
    entries.push_back(entry);
    BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Usable RAM (below UEFI)",
             (unsigned long long)entry.address,
             (unsigned long long)(entry.address + entry.length - 1),
             (unsigned long long)(entry.length / (1024*1024))));
  } else {
    // RAM doesn't reach UEFI ROM area, single entry
    entry.address = high_mem_start;
    entry.length = high_mem_end - high_mem_start;
    entry.type = E820_RAM;
    entries.push_back(entry);
    BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Usable RAM",
             (unsigned long long)entry.address,
             (unsigned long long)(entry.address + entry.length - 1),
             (unsigned long long)(entry.length / (1024*1024))));
  }

  // Entry 4: UEFI ROM area (0xFFC00000 - 0xFFFFFFFF = 4MB)
  // This is where OVMF firmware is loaded
  entry.address = 0xFFC00000;
  entry.length = 0x00400000;  // 4MB
  entry.type = E820_RESERVED;
  entries.push_back(entry);
  BX_DEBUG(("E820: 0x%08llx-0x%08llx (%llu MB) - Reserved (UEFI ROM)",
           (unsigned long long)entry.address,
           (unsigned long long)(entry.address + entry.length - 1),
           (unsigned long long)(entry.length / (1024*1024))));

  // Allocate memory for E820 data
  Bit32u e820_size = entries.size() * sizeof(E820Entry);
  Bit8u *e820_data = new Bit8u[e820_size];

  // Copy entries to data buffer (already in little-endian format on x86)
  memcpy(e820_data, entries.data(), e820_size);

  // Add E820 map as a file "etc/e820"
  add_file("etc/e820", e820_data, e820_size, false);

  BX_INFO(("E820 memory map: %u entries, %u bytes", (unsigned)entries.size(), e820_size));
}

#endif // BX_SUPPORT_PCI
