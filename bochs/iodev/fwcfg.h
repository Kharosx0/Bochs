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
//  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA B 02110-1301 USA
//
/////////////////////////////////////////////////////////////////////////

#ifndef BX_IODEV_FWCFG_H
#define BX_IODEV_FWCFG_H

#include <vector>

// QEMU fw_cfg device - Firmware Configuration Interface
// This device provides a simple interface for firmware (OVMF/UEFI) to
// retrieve platform configuration data from the emulator.

// Selector values for architecture-independent items
#define FW_CFG_SIGNATURE        0x0000  // Returns "QEMU"
#define FW_CFG_ID               0x0001  // Interface version
#define FW_CFG_UUID             0x0002  // VM UUID
#define FW_CFG_RAM_SIZE         0x0003  // RAM size in bytes
#define FW_CFG_NOGRAPHIC        0x0004  // Graphics mode
#define FW_CFG_NB_CPUS          0x0005  // Number of CPUs
#define FW_CFG_MACHINE_ID       0x0006  // Machine ID
#define FW_CFG_KERNEL_ADDR      0x0007  // Kernel address (direct boot)
#define FW_CFG_KERNEL_SIZE      0x0008  // Kernel size
#define FW_CFG_KERNEL_CMDLINE   0x0009  // Kernel command line
#define FW_CFG_INITRD_ADDR      0x000A  // Initrd address
#define FW_CFG_INITRD_SIZE      0x000B  // Initrd size
#define FW_CFG_BOOT_DEVICE      0x000C  // Boot device
#define FW_CFG_NUMA             0x000D  // NUMA data
#define FW_CFG_BOOT_MENU        0x000E  // Boot menu
#define FW_CFG_MAX_CPUS         0x000F  // Maximum CPUs
#define FW_CFG_KERNEL_ENTRY     0x0010  // Kernel entry point
#define FW_CFG_KERNEL_DATA      0x0011  // Kernel data
#define FW_CFG_INITRD_DATA      0x0012  // Initrd data
#define FW_CFG_CMDLINE_ADDR     0x0013  // Command line address
#define FW_CFG_CMDLINE_SIZE     0x0014  // Command line size
#define FW_CFG_CMDLINE_DATA     0x0015  // Command line data
#define FW_CFG_SETUP_ADDR       0x0016  // Setup address
#define FW_CFG_SETUP_SIZE       0x0017  // Setup size
#define FW_CFG_SETUP_DATA       0x0018  // Setup data
#define FW_CFG_FILE_DIR         0x0019  // File directory

// I/O port addresses
#define FW_CFG_PORT_SEL         0x0510  // Selector register (16-bit write)
#define FW_CFG_PORT_DATA        0x0511  // Data register (8-bit read/write)
#define FW_CFG_PORT_DMA         0x0514  // DMA address (32-bit write) - not implemented yet

// E820 memory map entry types
#define E820_RAM        1  // Usable RAM
#define E820_RESERVED   2  // Reserved
#define E820_ACPI       3  // ACPI Reclaimable
#define E820_NVS        4  // ACPI NVS
#define E820_UNUSABLE   5  // Unusable

// E820 memory map entry (20 bytes)
struct E820Entry {
    Bit64u address;  // Base address
    Bit64u length;   // Length in bytes
    Bit32u type;     // Memory type (E820_RAM, E820_RESERVED, etc.)
} GCC_ATTRIBUTE((packed));

// ==================================================================
// ACPI Table Structures
// ==================================================================

// ACPI Table Header (common to all ACPI tables)
struct ACPITableHeader {
    Bit8u  signature[4];         // Table signature (4 ASCII characters)
    Bit32u length;                // Table length including header
    Bit8u  revision;              // ACPI spec minor version
    Bit8u  checksum;              // Checksum of entire table (must sum to 0)
    Bit8u  oem_id[6];             // OEM ID
    Bit8u  oem_table_id[8];       // OEM table ID
    Bit32u oem_revision;          // OEM revision
    Bit8u  asl_compiler_id[4];    // ASL compiler ID
    Bit32u asl_compiler_revision; // ASL compiler revision
} GCC_ATTRIBUTE((packed));

// RSDP (Root System Description Pointer) - ACPI 1.0 version
struct ACPIRSDP {
    Bit8u  signature[8];           // "RSD PTR " (note the space)
    Bit8u  checksum;                // Checksum of bytes 0-19
    Bit8u  oem_id[6];               // OEM ID
    Bit8u  revision;                // 0 for ACPI 1.0, 2 for ACPI 2.0+
    Bit32u rsdt_physical_address;   // 32-bit physical address of RSDT
    // ACPI 2.0+ fields (we use ACPI 1.0 for simplicity)
    Bit32u length;                  // XSDT length
    Bit64u xsdt_physical_address;   // 64-bit address of XSDT
    Bit8u  extended_checksum;       // Extended checksum
    Bit8u  reserved[3];             // Reserved
} GCC_ATTRIBUTE((packed));

// RSDT (Root System Description Table)
struct ACPIRSTD {
    ACPITableHeader header;
    Bit32u entry[4];  // Pointers to other ACPI tables (FADT, MADT, etc.)
} GCC_ATTRIBUTE((packed));

// FACS (Firmware ACPI Control Structure)
struct ACPIFACS {
    Bit8u  signature[4];             // "FACS"
    Bit32u length;                    // Length of structure (64 bytes)
    Bit32u hardware_signature;        // Hardware signature
    Bit32u firmware_waking_vector;    // Firmware waking vector
    Bit32u global_lock;               // Global lock
    Bit32u flags;                     // Flags (bit 0 = S4BIOS_F)
    Bit8u  reserved[40];              // Reserved
} GCC_ATTRIBUTE((packed));

// FADT (Fixed ACPI Description Table) - ACPI 1.0 version
struct ACPIFADT {
    ACPITableHeader header;
    Bit32u firmware_ctrl;        // Physical address of FACS
    Bit32u dsdt;                  // Physical address of DSDT
    Bit8u  model;                 // System model (1 = PC/AT)
    Bit8u  reserved1;             // Reserved
    Bit16u sci_int;               // SCI interrupt number
    Bit32u smi_cmd;               // SMI command port
    Bit8u  acpi_enable;           // Value to enable ACPI
    Bit8u  acpi_disable;          // Value to disable ACPI
    Bit8u  S4bios_req;            // Value for S4 BIOS request
    Bit8u  reserved2;             // Reserved
    Bit32u pm1a_evt_blk;          // PM1a event register block address
    Bit32u pm1b_evt_blk;          // PM1b event register block address
    Bit32u pm1a_cnt_blk;          // PM1a control register block address
    Bit32u pm1b_cnt_blk;          // PM1b control register block address
    Bit32u pm2_cnt_blk;           // PM2 control register block address
    Bit32u pm_tmr_blk;            // PM timer register block address
    Bit32u gpe0_blk;              // GPE0 register block address
    Bit32u gpe1_blk;              // GPE1 register block address
    Bit8u  pm1_evt_len;           // PM1 event register block length
    Bit8u  pm1_cnt_len;           // PM1 control register block length
    Bit8u  pm2_cnt_len;           // PM2 control register block length
    Bit8u  pm_tmr_len;            // PM timer register block length
    Bit8u  gpe0_blk_len;          // GPE0 register block length
    Bit8u  gpe1_blk_len;          // GPE1 register block length
    Bit8u  gpe1_base;             // GPE1 base offset
    Bit8u  reserved3;             // Reserved
    Bit16u plvl2_lat;             // C2 latency
    Bit16u plvl3_lat;             // C3 latency
    Bit16u flush_size;            // Cache flush size
    Bit16u flush_stride;          // Cache flush stride
    Bit8u  duty_offset;           // Duty cycle offset
    Bit8u  duty_width;            // Duty cycle width
    Bit8u  day_alrm;              // RTC day alarm index
    Bit8u  mon_alrm;              // RTC month alarm index
    Bit8u  century;               // RTC century index
    Bit8u  reserved4[3];          // Reserved
    Bit32u flags;                 // Feature flags
} GCC_ATTRIBUTE((packed));

// MADT (Multiple APIC Description Table) header
struct ACPIMADT {
    ACPITableHeader header;
    Bit32u local_apic_address;   // Local APIC address (0xFEE00000)
    Bit32u flags;                 // Flags (bit 0 = PC-AT compatible)
    // Followed by variable-length APIC structures
} GCC_ATTRIBUTE((packed));

// MADT sub-structure: Processor Local APIC
#define ACPI_MADT_TYPE_LOCAL_APIC  0
struct MADTProcessorAPIC {
    Bit8u  type;                 // 0 = Processor Local APIC
    Bit8u  length;               // 8 bytes
    Bit8u  processor_id;         // ACPI processor ID
    Bit8u  apic_id;              // Local APIC ID
    Bit32u flags;                // Flags (bit 0 = enabled)
} GCC_ATTRIBUTE((packed));

// MADT sub-structure: I/O APIC
#define ACPI_MADT_TYPE_IO_APIC     1
struct MADTIOAPIC {
    Bit8u  type;                 // 1 = I/O APIC
    Bit8u  length;               // 12 bytes
    Bit8u  io_apic_id;           // I/O APIC ID
    Bit8u  reserved;             // Reserved (0)
    Bit32u io_apic_address;      // I/O APIC address (0xFEC00000)
    Bit32u global_irq_base;      // Global system interrupt base
} GCC_ATTRIBUTE((packed));

// MADT sub-structure: Interrupt Source Override
#define ACPI_MADT_TYPE_IRQ_OVERRIDE 2
struct MADTIRQOverride {
    Bit8u  type;                 // 2 = Interrupt Source Override
    Bit8u  length;               // 10 bytes
    Bit8u  bus;                  // Bus (0 = ISA)
    Bit8u  source;               // Source IRQ
    Bit32u gsi;                  // Global System Interrupt
    Bit16u flags;                // MPS INTI flags
} GCC_ATTRIBUTE((packed));

// HPET (High Precision Event Timer) Table
struct ACPIHPET {
    ACPITableHeader header;
    Bit32u timer_block_id;       // Timer block ID
    Bit8u  address_space_id;     // Address space (0 = memory)
    Bit8u  register_bit_width;   // Register width
    Bit8u  register_bit_offset;  // Register offset
    Bit8u  access_size;          // Access size
    Bit64u address;              // HPET address (0xFED00000)
    Bit8u  hpet_number;          // HPET sequence number
    Bit16u min_tick;             // Minimum tick
    Bit8u  page_protect;         // Page protection
} GCC_ATTRIBUTE((packed));

// ACPI constants
#define ACPI_HPET_ADDRESS    0xFED00000
#define ACPI_IOAPIC_ADDRESS  0xFEC00000
#define ACPI_LAPIC_ADDRESS   0xFEE00000

// ==================================================================
// SMBIOS Table Structures
// ==================================================================

// SMBIOS Entry Point Structure (must be 16-byte aligned)
struct SMBIOSEntryPoint {
    Bit8u  anchor_string[4];           // "_SM_"
    Bit8u  checksum;                   // Entry point checksum
    Bit8u  length;                     // Entry point length (0x1F)
    Bit8u  smbios_major_version;       // SMBIOS major version (2)
    Bit8u  smbios_minor_version;       // SMBIOS minor version (4)
    Bit16u max_structure_size;         // Maximum size of SMBIOS structure
    Bit8u  entry_point_revision;       // Entry point revision (0)
    Bit8u  formatted_area[5];          // Formatted area
    Bit8u  intermediate_anchor[5];     // "_DMI_"
    Bit8u  intermediate_checksum;      // Intermediate checksum
    Bit16u structure_table_length;     // Structure table length
    Bit32u structure_table_address;    // Structure table address
    Bit16u number_of_structures;       // Number of SMBIOS structures
    Bit8u  smbios_bcd_revision;        // SMBIOS BCD revision
} GCC_ATTRIBUTE((packed));

// Common header for all SMBIOS structures
struct SMBIOSStructureHeader {
    Bit8u  type;      // Structure type
    Bit8u  length;    // Structure length (excluding strings)
    Bit16u handle;    // Structure handle
} GCC_ATTRIBUTE((packed));

// Type 0: BIOS Information
struct SMBIOSType0 {
    SMBIOSStructureHeader header;
    Bit8u  vendor_str;                        // String index
    Bit8u  bios_version_str;                  // String index
    Bit16u bios_starting_address_segment;     // Usually 0xE000
    Bit8u  bios_release_date_str;             // String index
    Bit8u  bios_rom_size;                     // (size-1) / 64KB
    Bit64u bios_characteristics;              // Capabilities bitmap
    Bit8u  bios_characteristics_ext_bytes[2]; // Extended capabilities
    Bit8u  system_bios_major_release;
    Bit8u  system_bios_minor_release;
    Bit8u  embedded_controller_major_release;
    Bit8u  embedded_controller_minor_release;
} GCC_ATTRIBUTE((packed));

// Type 1: System Information
struct SMBIOSType1 {
    SMBIOSStructureHeader header;
    Bit8u  manufacturer_str;       // String index
    Bit8u  product_name_str;       // String index
    Bit8u  version_str;            // String index
    Bit8u  serial_number_str;      // String index
    Bit8u  uuid[16];               // UUID (same as fw_cfg UUID)
    Bit8u  wake_up_type;           // Wake-up type
    Bit8u  sku_number_str;         // String index
    Bit8u  family_str;             // String index
} GCC_ATTRIBUTE((packed));

// Type 3: System Enclosure
struct SMBIOSType3 {
    SMBIOSStructureHeader header;
    Bit8u  manufacturer_str;       // String index
    Bit8u  type;                   // Enclosure type (3 = Desktop)
    Bit8u  version_str;            // String index
    Bit8u  serial_number_str;      // String index
    Bit8u  asset_tag_number_str;   // String index
    Bit8u  boot_up_state;          // State when booting
    Bit8u  power_supply_state;     // Power supply state
    Bit8u  thermal_state;          // Thermal state
    Bit8u  security_status;        // Security status
    Bit32u oem_defined;            // OEM-specific
    Bit8u  height;                 // Height in U units
    Bit8u  number_of_power_cords;  // Number of power cords
    Bit8u  contained_element_count;// Number of contained elements
} GCC_ATTRIBUTE((packed));

// Type 4: Processor Information
struct SMBIOSType4 {
    SMBIOSStructureHeader header;
    Bit8u  socket_designation_str;   // String index
    Bit8u  processor_type;           // Processor type (3 = Central Processor)
    Bit8u  processor_family;         // Processor family
    Bit8u  processor_manufacturer_str; // String index
    Bit32u processor_id[2];          // Processor ID from CPUID
    Bit8u  processor_version_str;    // String index
    Bit8u  voltage;                  // Voltage
    Bit16u external_clock;           // External clock in MHz
    Bit16u max_speed;                // Max speed in MHz
    Bit16u current_speed;            // Current speed in MHz
    Bit8u  status;                   // Status (enabled/disabled)
    Bit8u  processor_upgrade;        // Processor upgrade
} GCC_ATTRIBUTE((packed));

// Type 16: Physical Memory Array
struct SMBIOSType16 {
    SMBIOSStructureHeader header;
    Bit8u  location;                         // Location (3 = System board)
    Bit8u  use;                              // Use (3 = System memory)
    Bit8u  error_correction;                 // Error correction (3 = None)
    Bit32u maximum_capacity;                 // Maximum capacity in KB
    Bit16u memory_error_information_handle;  // 0xFFFE = Not provided
    Bit16u number_of_memory_devices;         // Number of devices
} GCC_ATTRIBUTE((packed));

// Type 17: Memory Device
struct SMBIOSType17 {
    SMBIOSStructureHeader header;
    Bit16u physical_memory_array_handle;    // Handle of type 16
    Bit16u memory_error_information_handle; // 0xFFFE = Not provided
    Bit16u total_width;                     // Total width in bits
    Bit16u data_width;                      // Data width in bits
    Bit16u size;                            // Size in MB
    Bit8u  form_factor;                     // Form factor (9 = DIMM)
    Bit8u  device_set;                      // Device set (0 = None)
    Bit8u  device_locator_str;              // String index
    Bit8u  bank_locator_str;                // String index
    Bit8u  memory_type;                     // Memory type (7 = SDRAM)
    Bit16u type_detail;                     // Type detail
} GCC_ATTRIBUTE((packed));

// Type 19: Memory Array Mapped Address
struct SMBIOSType19 {
    SMBIOSStructureHeader header;
    Bit32u starting_address;               // Starting address in KB
    Bit32u ending_address;                 // Ending address in KB
    Bit16u memory_array_handle;            // Handle of type 16
    Bit8u  partition_width;                // Number of devices
} GCC_ATTRIBUTE((packed));

// Type 20: Memory Device Mapped Address
struct SMBIOSType20 {
    SMBIOSStructureHeader header;
    Bit32u starting_address;                       // Starting address in KB
    Bit32u ending_address;                         // Ending address in KB
    Bit16u memory_device_handle;                   // Handle of type 17
    Bit16u memory_array_mapped_address_handle;     // Handle of type 19
    Bit8u  partition_row_position;                 // Position in partition
    Bit8u  interleave_position;                    // Interleave position
    Bit8u  interleaved_data_depth;                 // Data depth
} GCC_ATTRIBUTE((packed));

// Type 32: System Boot Information
struct SMBIOSType32 {
    SMBIOSStructureHeader header;
    Bit8u  reserved[6];            // Reserved
    Bit8u  boot_status;            // Boot status (0 = No errors)
} GCC_ATTRIBUTE((packed));

// Type 127: End-of-Table
struct SMBIOSType127 {
    SMBIOSStructureHeader header;
} GCC_ATTRIBUTE((packed));

// ==================================================================
// BIOS Linker/Loader Structures (for etc/table-loader)
// ==================================================================

#define BIOS_LINKER_LOADER_FILESZ 56

// Linker/loader command types
#define BIOS_LINKER_LOADER_COMMAND_ALLOCATE     0x1
#define BIOS_LINKER_LOADER_COMMAND_ADD_POINTER  0x2
#define BIOS_LINKER_LOADER_COMMAND_ADD_CHECKSUM 0x3

// Allocation zone types
#define BIOS_LINKER_LOADER_ALLOC_ZONE_HIGH 0x1  // High memory
#define BIOS_LINKER_LOADER_ALLOC_ZONE_FSEG 0x2  // F-segment (0xF0000-0xFFFFF)

// Linker/loader command entry (128 bytes total)
struct BiosLinkerLoaderEntry {
    Bit32u command;  // Command type
    union {
        // ALLOCATE command - allocate memory for a blob
        struct {
            char file[BIOS_LINKER_LOADER_FILESZ];  // Filename to allocate
            Bit32u align;                           // Alignment (power of 2)
            Bit8u zone;                            // Allocation zone (HIGH or FSEG)
        } alloc;

        // ADD_POINTER command - patch pointer from one blob to another
        struct {
            char dest_file[BIOS_LINKER_LOADER_FILESZ];  // Destination file
            char src_file[BIOS_LINKER_LOADER_FILESZ];   // Source file (target)
            Bit32u offset;                               // Offset in dest_file to patch
            Bit8u size;                                 // Pointer size (1, 2, 4, or 8 bytes)
        } pointer;

        // ADD_CHECKSUM command - calculate and store checksum
        struct {
            char file[BIOS_LINKER_LOADER_FILESZ];  // File to checksum
            Bit32u offset;                          // Where to store checksum
            Bit32u start;                           // Start of checksummed region
            Bit32u length;                          // Length of checksummed region
        } cksum;

        Bit8u pad[124];  // Padding to ensure 128-byte total size
    };
} GCC_ATTRIBUTE((packed));

// File directory entry (64 bytes)
struct FWCfgFile {
    Bit32u size;          // File size (big-endian)
    Bit16u select;        // Selector value (big-endian)
    Bit16u reserved;      // Reserved (must be 0)
    char name[56];        // NUL-terminated filename
};

#if BX_SUPPORT_PCI

// Internal file entry
struct FWCfgEntry {
    Bit16u selector;
    char name[56];
    Bit8u *data;
    Bit32u size;
    bool writable;
};

class bx_fwcfg_c : public bx_devmodel_c {
public:
    bx_fwcfg_c();
    virtual ~bx_fwcfg_c();
    virtual void init(void);
    virtual void reset(unsigned type);
    virtual void register_state(void);

    // I/O port handlers
    static Bit32u read_handler(void *this_ptr, Bit32u address, unsigned io_len);
    static void write_handler(void *this_ptr, Bit32u address, Bit32u value, unsigned io_len);

    // API for adding file-based items
    void add_file(const char *name, Bit8u *data, Bit32u size, bool writable = false);

private:
    struct {
        Bit16u cur_selector;      // Currently selected item
        Bit32u cur_offset;        // Current read/write offset within selected item

        // File-based interface
        std::vector<FWCfgEntry> files;
        Bit16u next_file_selector;  // Next available selector (starts at 0x20)

        // Cached file directory (generated on demand)
        Bit8u *file_dir_data;
        Bit32u file_dir_size;

        // Static data for basic selectors
        Bit8u signature[4];       // "QEMU"
        Bit32u interface_version;  // Version identifier
        Bit8u uuid[16];           // VM UUID
        Bit64u ram_size;          // RAM size in bytes
        Bit16u nographic;         // Graphics mode (0=graphic, 1=nogui)
        Bit16u nb_cpus;           // Number of CPUs
        Bit16u max_cpus;          // Maximum CPUs
        Bit16u machine_id;        // Machine type (1=PC)

    } s;

    // Internal methods
    Bit32u read(Bit32u address, unsigned io_len);
    void write(Bit32u address, Bit32u value, unsigned io_len);
    Bit8u read_byte();
    void write_byte(Bit8u value);
    void select_item(Bit16u selector);
    void generate_file_directory();
    int find_file_by_selector(Bit16u selector);
    void cleanup_file_directory();
    void generate_e820_map();
    void generate_acpi_tables();
    Bit8u acpi_checksum(void *data, Bit32u length);
    void acpi_build_table_header(ACPITableHeader *h, const char *sig, Bit32u len, Bit8u rev);
    void generate_acpi_loader();
    void generate_smbios_tables();
    Bit8u smbios_checksum(void *data, Bit32u length);
    void generate_bootorder();
};

#endif // BX_SUPPORT_PCI

#endif // BX_IODEV_FWCFG_H
