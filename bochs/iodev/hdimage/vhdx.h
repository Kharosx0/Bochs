/////////////////////////////////////////////////////////////////////////
// $Id$
/////////////////////////////////////////////////////////////////////////
//
// Block driver for Microsoft Hyper-V VHDX images
// Based on VHDX Format Specification v1.00 and QEMU implementation
//
// Copyright (C) 2013 Red Hat, Inc. (original QEMU code)
// Copyright (C) 2025 The Bochs Project
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
/////////////////////////////////////////////////////////////////////////

#ifndef BX_VHDXIMG_H
#define BX_VHDXIMG_H

// VHDX Format Constants
#define VHDX_FILE_SIGNATURE "vhdxfile"
#define VHDX_HEADER_SIGNATURE 0x64616568  // "head"
#define VHDX_REGION_SIGNATURE 0x69676572  // "regi"

// File layout offsets (all in bytes)
#define VHDX_FILE_ID_OFFSET      0
#define VHDX_HEADER1_OFFSET      (64 * 1024)
#define VHDX_HEADER2_OFFSET      (128 * 1024)
#define VHDX_REGION_TABLE_OFFSET (192 * 1024)
#define VHDX_HEADER_SIZE         (4 * 1024)
#define VHDX_REGION_TABLE_SIZE   (64 * 1024)

// Block size limits
#define VHDX_BLOCK_SIZE_MIN      (1 * 1024 * 1024)    // 1 MB
#define VHDX_BLOCK_SIZE_MAX      (256 * 1024 * 1024)  // 256 MB

// BAT entry constants
#define VHDX_BAT_STATE_BIT_MASK         0x07
#define VHDX_BAT_FILE_OFF_MASK          0xFFFFFFFFFFF00000ULL  // Upper 44 bits
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_NOT_PRESENT        0
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_UNDEFINED          1
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_ZERO               2
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_UNMAPPED           3
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_FULLY_PRESENT      6
#define VHDX_BAT_STATE_PAYLOAD_BLOCK_PARTIALLY_PRESENT  7

// Known region GUIDs
#define VHDX_REGION_BAT_GUID \
  { 0x2d, 0xc2, 0x76, 0x66, 0xf6, 0x23, 0xd5, 0x11, \
    0x99, 0x3b, 0x00, 0x1e, 0xc0, 0x6d, 0x61, 0x02 }

#define VHDX_REGION_METADATA_GUID \
  { 0x06, 0xa2, 0x76, 0x66, 0xf6, 0x23, 0xd5, 0x11, \
    0x99, 0x93, 0x00, 0x1e, 0xc0, 0x6d, 0x61, 0x02 }

// Metadata item GUIDs
#define VHDX_METADATA_FILE_PARAMS_GUID \
  { 0x37, 0xc9, 0xa2, 0xca, 0xe3, 0x4b, 0xd7, 0x45, \
    0x93, 0xef, 0xc3, 0x09, 0xe0, 0x00, 0xc7, 0x46 }

#define VHDX_METADATA_VIRTUAL_DISK_SIZE_GUID \
  { 0x24, 0x24, 0x2c, 0x24, 0x36, 0x49, 0xbc, 0x40, \
    0x8b, 0x99, 0xa8, 0x87, 0xdd, 0x13, 0x0d, 0x49 }

#define VHDX_METADATA_LOGICAL_SECTOR_SIZE_GUID \
  { 0x1d, 0xbf, 0x41, 0x81, 0xf9, 0x6f, 0xbf, 0x47, \
    0xb9, 0x2e, 0x08, 0x20, 0xab, 0x7f, 0x6c, 0x12 }

#define VHDX_METADATA_PHYSICAL_SECTOR_SIZE_GUID \
  { 0xcd, 0x76, 0xdc, 0xcd, 0xf6, 0x51, 0xc7, 0x47, \
    0x9c, 0xc9, 0x5a, 0x07, 0x79, 0xba, 0xb5, 0xc3 }

// Endian conversion for little-endian VHDX format
#if defined(BX_LITTLE_ENDIAN)
#define le16_to_cpu(val) (val)
#define le32_to_cpu(val) (val)
#define le64_to_cpu(val) (val)
#define cpu_to_le16(val) (val)
#define cpu_to_le32(val) (val)
#define cpu_to_le64(val) (val)
#else
#define le16_to_cpu(val) bx_bswap16(val)
#define le32_to_cpu(val) bx_bswap32(val)
#define le64_to_cpu(val) bx_bswap64(val)
#define cpu_to_le16(val) bx_bswap16(val)
#define cpu_to_le32(val) bx_bswap32(val)
#define cpu_to_le64(val) bx_bswap64(val)
#endif

Bit32u vhdx_checksum(Bit8u *buf, size_t size);
bool guid_eq(const Bit8u *guid1, const Bit8u *guid2);

#if defined(_MSC_VER) && (_MSC_VER<1300)
#pragma pack(push, 1)
#elif defined(__MWERKS__) && defined(macintosh)
#pragma options align=packed
#endif

// All structures are little-endian
typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_file_identifier_t {
    Bit64u signature;         // "vhdxfile"
    Bit16u creator[256];      // UTF-16 creator string (optional)
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_file_identifier_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_header_t {
    Bit32u signature;         // "head"
    Bit32u checksum;          // CRC-32C
    Bit64u sequence_number;   // Incremented on each header update
    Bit8u  file_write_guid[16];
    Bit8u  data_write_guid[16];
    Bit8u  log_guid[16];
    Bit16u log_version;
    Bit16u version;
    Bit32u log_length;
    Bit64u log_offset;
    Bit8u  reserved[4016];    // Pad to 4KB
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_header_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_region_table_header_t {
    Bit32u signature;         // "regi"
    Bit32u checksum;          // CRC-32C
    Bit32u entry_count;       // Number of valid entries
    Bit32u reserved;
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_region_table_header_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_region_table_entry_t {
    Bit8u  guid[16];          // Region GUID
    Bit64u file_offset;       // Offset in file (1MB aligned)
    Bit32u length;            // Length in bytes
    Bit32u required;          // Bit 0: required flag
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_region_table_entry_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_metadata_table_header_t {
    Bit64u signature;         // "metadata"
    Bit16u reserved;
    Bit16u entry_count;       // Number of entries
    Bit32u reserved2[5];
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_metadata_table_header_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_metadata_table_entry_t {
    Bit8u  item_id[16];       // Item GUID
    Bit32u offset;            // Offset within metadata region
    Bit32u length;            // Length in bytes
    Bit32u flags;             // IsUser, IsVirtualDisk, IsRequired
    Bit32u reserved;
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_metadata_table_entry_t;

typedef
#if defined(_MSC_VER) && (_MSC_VER>=1300)
__declspec(align(1))
#endif
struct vhdx_file_parameters_t {
    Bit32u block_size;        // Block size in bytes
    Bit32u flags;             // Leave blocks allocated, has parent
}
#if !defined(_MSC_VER)
GCC_ATTRIBUTE((packed))
#endif
vhdx_file_parameters_t;

#if defined(_MSC_VER) && (_MSC_VER<1300)
#pragma pack(pop)
#elif defined(__MWERKS__) && defined(macintosh)
#pragma options align=reset
#endif

class vhdx_image_t : public device_image_t
{
  public:
    int open(const char* pathname, int flags);
    void close();
    Bit64s lseek(Bit64s offset, int whence);
    ssize_t read(void* buf, size_t count);
    ssize_t write(const void* buf, size_t count);

    Bit32u get_capabilities();
    static int check_format(int fd, Bit64u imgsize);

#ifdef BXIMAGE
    int create_image(const char *pathname, Bit64u size);
#else
    bool save_state(const char *backup_fname);
    void restore_state(const char *backup_fname);
#endif

  private:
    int parse_header();
    int parse_region_table();
    int parse_metadata();
    Bit64s get_sector_offset(Bit64s sector_num);
    int validate_checksum(Bit8u *buf, size_t size, Bit32u expected_crc);

    int fd;
    Bit64s sector_count;
    Bit64s cur_sector;
    Bit64u virtual_disk_size;
    Bit32u logical_sector_size;
    Bit32u physical_sector_size;
    Bit32u block_size;
    Bit32u sectors_per_block;
    Bit64u bat_offset;
    Bit32u bat_entry_count;
    Bit64u *bat;              // Block Allocation Table

    const char *pathname;
};

#endif
