#ifndef ISO9660_H
#define ISO9660_H

#include <stdint.h>
#include <stdbool.h>

// Forward declaration
struct Cdrom;
struct IsoPrimaryVolumeDescriptor;
struct IsoDirectoryRecord;

#define ISO_SECTOR_SIZE 2048
#define PVD_SECTOR      16

typedef struct __attribute__((packed)) {
    uint8_t  type_code;
    char     standard_identifier[5];
    uint8_t  version;
    uint8_t  _unused1[1];
    char     system_identifier[32];
    char     volume_identifier[32];
    uint8_t  _unused2[8];
    uint32_t volume_space_size_le;
    uint32_t volume_space_size_be;
    uint8_t  _unused3[32];
    uint16_t volume_set_size_le;
    uint16_t volume_set_size_be;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint16_t logical_block_size_le;
    uint16_t logical_block_size_be;
    uint32_t path_table_size_le;
    uint32_t path_table_size_be;
    uint32_t path_table_l_loc_le;
    uint32_t path_table_opt_l_loc_le;
    uint32_t path_table_m_loc_be;
    uint32_t path_table_opt_m_loc_be;
    uint8_t  root_directory_record[34];
} IsoPrimaryVolumeDescriptor;

typedef struct __attribute__((packed)) {
    uint8_t  length;
    uint8_t  extended_attribute_length;
    uint32_t extent_location_le;
    uint32_t extent_location_be;
    uint32_t data_length_le;
    uint32_t data_length_be;
    uint8_t  recording_datetime[7];
    uint8_t  file_flags;
    uint8_t  file_unit_size;
    uint8_t  interleave_gap_size;
    uint16_t volume_sequence_number_le;
    uint16_t volume_sequence_number_be;
    uint8_t  file_identifier_length;
    char     file_identifier[];
} IsoDirectoryRecord;

// --- Function Prototypes ---
// Note the use of "struct Cdrom*" here
bool iso_read_pvd(struct Cdrom* cdrom, IsoPrimaryVolumeDescriptor* pvd);
bool iso_find_file(struct Cdrom* cdrom, IsoDirectoryRecord* directory_record, const char* filename, IsoDirectoryRecord* found_record);

#endif // ISO9660_H