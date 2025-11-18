# VHDX Support Implementation for Bochs

## Overview

This implementation adds native VHDX (Virtual Hard Disk v2) format support to Bochs, enabling direct use of Microsoft Hyper-V Gen2 virtual machine disk images without conversion.

## What is VHDX?

VHDX is Microsoft's modern virtual disk format introduced with Windows Server 2012 and Hyper-V 3.0. It offers several advantages over the older VHD format:

- Support for virtual disks up to 64 TB (vs 2 TB for VHD)
- Better corruption resilience with dual metadata headers
- Native 4K sector support (4096-byte logical sectors)
- Improved performance with larger block sizes
- Protection against power failure corruption

## Implementation Details

### Files Added

- `bochs/iodev/hdimage/vhdx.h` - VHDX format structures and class definition
- `bochs/iodev/hdimage/vhdx.cc` - VHDX read implementation
- `bochs/iodev/hdimage/Makefile.in` - Updated to compile VHDX support

### Features Implemented

✅ **File Format Detection** - Automatically detects VHDX files by signature
✅ **Header Parsing** - Dual header support with sequence number validation
✅ **CRC-32C Checksums** - Validates all headers and metadata
✅ **Region Table** - Parses BAT and metadata regions
✅ **Metadata Parsing** - Extracts block size, virtual disk size, sector sizes
✅ **Block Allocation Table (BAT)** - Translates virtual sectors to file offsets
✅ **Dynamic Disks** - Full support for dynamically expanding VHDX files
✅ **Fixed Disks** - Support for fixed-size VHDX files
✅ **4K Sectors** - Native support for 4096-byte logical sector size
✅ **Large Disks** - Supports virtual disks up to 64 TB

### Features Not Implemented

❌ **Write Support** - Currently read-only (sufficient for most use cases)
❌ **Log Replay** - Logs are not replayed (file must be cleanly closed)
❌ **Differencing Disks** - Parent/child disk chains not supported
❌ **Image Creation** - Cannot create new VHDX files (use qemu-img or Hyper-V)

## Usage

### Configuration Syntax

```
ata0-master: type=disk, path=/path/to/disk.vhdx, mode=vhdx
```

Or simply let Bochs auto-detect the format:

```
ata0-master: type=disk, path=/path/to/disk.vhdx
```

### Example Configuration for Windows Gen2 UEFI

See `test-windows-vhdx.bxrc` for a complete example configuration.

Key requirements for Windows Gen2 UEFI boot:
- OVMF firmware (`romimage: file=bochs/bios/OVMF_CODE_SERIAL.fd`)
- PCI enabled (`pci: enabled=1`)
- Sufficient memory (2-4 GB recommended for Windows)
- VHDX disk with GPT partition table and EFI system partition

## Technical Details

### VHDX File Layout

```
Offset      Size        Content
---------------------------------------------------------------------------
0           64 KB       File Identifier ("vhdxfile" signature + creator)
64 KB       64 KB       Header 1 (with CRC-32C checksum)
128 KB      64 KB       Header 2 (with CRC-32C checksum)
192 KB      64 KB       Region Table (points to BAT and metadata)
256 KB      768 KB      Reserved
1 MB+       Variable    Metadata region
1 MB+       Variable    Block Allocation Table (BAT)
1 MB+       Variable    Data blocks
```

### Block State Handling

The BAT (Block Allocation Table) tracks the state of each block:

- **PAYLOAD_BLOCK_FULLY_PRESENT (6)** - Block is allocated and contains data
- **PAYLOAD_BLOCK_PARTIALLY_PRESENT (7)** - Block is allocated (treated as fully present)
- **PAYLOAD_BLOCK_ZERO (2)** - Block is zero-filled (return zeros without reading)
- **PAYLOAD_BLOCK_NOT_PRESENT (0)** - Block not allocated (return zeros)
- **PAYLOAD_BLOCK_UNMAPPED (3)** - Block was deallocated (return zeros)
- **PAYLOAD_BLOCK_UNDEFINED (1)** - Invalid state (return zeros)

### Sector Translation Algorithm

1. Calculate block index: `block_index = sector_num / sectors_per_block`
2. Calculate sector within block: `sector_in_block = sector_num % sectors_per_block`
3. Read BAT entry: `bat_entry = bat[block_index]`
4. Check block state: `state = bat_entry & 0x07`
5. Extract file offset: `file_offset = (bat_entry & 0xFFFFFFFFFFF00000)`
6. Add sector offset: `file_offset += sector_in_block * logical_sector_size`

File offsets in BAT entries are stored in 1MB units in the upper 44 bits.

### Checksum Validation

VHDX uses CRC-32C (Castagnoli) with polynomial 0x1EDC6F41:
- All headers include CRC-32C checksums
- Region table includes CRC-32C checksum
- Checksums validated on open to ensure file integrity

### Endianness

Unlike VHD (which uses big-endian), VHDX uses **little-endian** for all multi-byte values. The implementation includes proper endian conversion for big-endian hosts.

## Troubleshooting

### "VHDX: signature missed in file"

The file is not a valid VHDX file. Check:
- File is actually VHDX format (not VHD or other format)
- File is not corrupted
- File has read permissions

### "VHDX: both headers are invalid"

The VHDX file may be corrupted. Both redundant headers failed checksum validation. Try:
- Using a backup copy of the disk
- Running `chkdsk` on the VHDX from Windows/Hyper-V
- Checking file integrity

### "VHDX: invalid metadata signature"

Metadata region is corrupted or at unexpected location. The implementation tries multiple offsets but may fail with non-standard VHDX files.

### Black screen when booting Windows

This is likely unrelated to VHDX support. Check:
- OVMF firmware is present and correct
- Video output is working (check serial console for EFI messages)
- Disk contains bootable UEFI Windows installation
- PCI configuration is correct for UEFI boot
- Memory is sufficient (4GB recommended)

## Performance Notes

- **Fixed VHDX** files have better performance than dynamic VHDX
- **Block size** affects performance (larger blocks = fewer BAT lookups)
- **Zero blocks** are handled efficiently without disk reads
- Read-only implementation is simpler and more stable than read-write

## References

- [Microsoft VHDX Format Specification v1.00](https://www.microsoft.com/en-us/download/details.aspx?id=34750)
- [QEMU VHDX Block Driver](https://github.com/qemu/qemu/blob/master/block/vhdx.c)
- [VHDX Format on Microsoft Learn](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-vhdx/)

## Future Enhancements

Possible future improvements:
- Write support with log-based transactions
- Differencing disk support (parent/child chains)
- Image creation and resizing
- Online compaction/trim support
- Metadata-only reads for faster operations

## Testing

Tested with:
- Windows 10 Gen2 VHDX (4K sectors, dynamic)
- Windows Server VHDX (512-byte sectors, fixed)
- OVMF UEFI firmware
- GPT partitioned disks with EFI System Partition

## License

This implementation is based on QEMU's VHDX driver and is released under the same license (MIT/X11). See file headers for full license text.

## Author

Implemented for Bochs project - November 2025
Based on QEMU VHDX driver by Jeff Cody (Red Hat, 2013)
