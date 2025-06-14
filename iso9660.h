#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stdbool.h>

struct Cdrom; //Forwarding

// --- ISO9660 Constants ---
#define ISO_SECTOR_SIZE 2048
#define PVD_SECTOR      16 // The Primary Volume Descriptor is always at sector 16

// --- ISO9660 Data Structures ---
// These structures must be packed to ensure they match the on-disk layout precisely.
// The __attribute__((packed)) directive tells the compiler not to add any padding.

/**
 * @brief Represents the ISO9660 Primary Volume Descriptor (PVD).
 * This is the "superblock" of the filesystem, containing master information.
 * We only need a few fields from it, primarily the location of the root directory record.
 */
typedef struct __attribute__((packed)) {
    uint8_t  type_code;                      // 01: Must be 1 for a PVD
    char     standard_identifier[5];         // 02-06: Must be "CD001"
    uint8_t  version;                        // 07: Must be 1
    uint8_t  _unused1[1];
    char     system_identifier[32];          // 09-40
    char     volume_identifier[32];          // 41-72
    uint8_t  _unused2[8];
    uint32_t volume_space_size_le;           // 81-84: Total sectors (Little Endian)
    uint32_t volume_space_size_be;           // 85-88: Total sectors (Big Endian)
    uint8_t  _unused3[32];
    uint16_t volume_set_size_le;             // 121-122
    uint16_t volume_set_size_be;             // 123-124
    uint16_t volume_sequence_number_le;      // 125-126
    uint16_t volume_sequence_number_be;      // 127-128
    uint16_t logical_block_size_le;          // 129-130: Should be 2048
    uint16_t logical_block_size_be;          // 131-132: Should be 2048
    uint32_t path_table_size_le;             // 133-136
    uint32_t path_table_size_be;             // 137-140
    uint32_t path_table_l_loc_le;            // 141-144
    uint32_t path_table_opt_l_loc_le;        // 145-148
    uint32_t path_table_m_loc_be;            // 149-152
    uint32_t path_table_opt_m_loc_be;        // 153-156
    uint8_t  root_directory_record[34];      // 157-190: The directory entry for the root "/"
    // ... other fields we don't need for now
} IsoPrimaryVolumeDescriptor;


/**
 * @brief Represents a single ISO9660 Directory Record.
 * Each file and directory on the disc has one of these records.
 */
typedef struct __attribute__((packed)) {
    uint8_t  length;                         // 01: Length of this directory record
    uint8_t  extended_attribute_length;      // 02
    uint32_t extent_location_le;             // 03-06: LBA of the file's data (Little Endian)
    uint32_t extent_location_be;             // 07-10: LBA of the file's data (Big Endian)
    uint32_t data_length_le;                 // 11-14: Size of the file in bytes (Little Endian)
    uint32_t data_length_be;                 // 15-18: Size of the file in bytes (Big Endian)
    uint8_t  recording_datetime[7];          // 19-25
    uint8_t  file_flags;                     // 26: Bit 1 indicates it's a directory
    uint8_t  file_unit_size;                 // 27
    uint8_t  interleave_gap_size;            // 28
    uint16_t volume_sequence_number_le;      // 29-30
    uint16_t volume_sequence_number_be;      // 31-32
    uint8_t  file_identifier_length;         // 33: Length of the file identifier string
    char     file_identifier[];              // 34...: The file/directory name itself (variable length)
} IsoDirectoryRecord;

/**
 * @brief Searches a directory on the disc for a specific file.
 *
 * @param cdrom Pointer to the Cdrom state.
 * @param directory_record Pointer to the record of the directory to search.
 * @param filename The name of the file to find (e.g., "SYSTEM.CNF;1").
 * @param found_record A pointer to a record that will be filled if the file is found.
 * @return True if the file was found, false otherwise.
 */
bool iso_find_file(Cdrom* cdrom, IsoDirectoryRecord* directory_record, const char* filename, IsoDirectoryRecord* found_record);

// --- Function Prototypes ---

// We will implement these functions in iso9660.c next.


#endif // ISO9660_H