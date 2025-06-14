#include "iso9660.h"
#include "cdrom.h"   // <<< ADD THIS INCLUDE
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
/**
 * @brief Reads a single sector from the CD-ROM disc image.
 *
 * @param cdrom Pointer to the Cdrom state, which contains the FILE* handle.
 * @param lba The Logical Block Address (sector number) to read.
 * @param buffer A pointer to a buffer of at least ISO_SECTOR_SIZE bytes.
 * @return True if the sector was read successfully, false otherwise.
 */
static bool read_sector(Cdrom* cdrom, uint32_t lba, uint8_t* buffer) {
    if (!cdrom->disc_present || !cdrom->disc_file) {
        fprintf(stderr, "ISO9660 Error: Cannot read sector, no disc file loaded.\n");
        return false;
    }

    // A sector in an ISO is 2048 bytes, but raw .bin files often use 2352.
    // For now, we assume the file is a standard ISO or we are seeking within
    // the data track of a raw image. We will refine this later.
    long long offset = (long long)lba * ISO_SECTOR_SIZE;

    if (fseek(cdrom->disc_file, (long)offset, SEEK_SET) != 0) {
        perror("ISO9660 Error: fseek failed");
        return false;
    }
    memset(buffer, 0, ISO_SECTOR_SIZE);

    size_t bytes_read = fread(buffer, 1, ISO_SECTOR_SIZE, cdrom->disc_file);

    if (bytes_read != ISO_SECTOR_SIZE) {
        fprintf(stderr, "ISO9660 Error: fread failed or read incomplete (%zu bytes).\n", bytes_read);
        return false;
    }

    return true;
}


/**
 * @brief Reads and validates the Primary Volume Descriptor from the disc.
 *
 * @param cdrom Pointer to the Cdrom state.
 * @param pvd A pointer to an IsoPrimaryVolumeDescriptor struct to be filled.
 * @return True if a valid PVD was found and read, false otherwise.
 */
bool iso_read_pvd(Cdrom* cdrom, IsoPrimaryVolumeDescriptor* pvd) {
    uint8_t sector_buffer[ISO_SECTOR_SIZE];

    printf("ISO9660: Reading Primary Volume Descriptor at sector %d...\n", PVD_SECTOR);

    if (!read_sector(cdrom, PVD_SECTOR, sector_buffer)) {
        fprintf(stderr, "ISO9660 Error: Failed to read PVD sector.\n");
        return false;
    }
    printf("ISO9660: Successfully read sector %d.\n", PVD_SECTOR);

    IsoPrimaryVolumeDescriptor* temp_pvd = (IsoPrimaryVolumeDescriptor*)sector_buffer;

    if (temp_pvd->type_code != 1) {
        fprintf(stderr, "ISO9660 Error: PVD type code is not 1 (was 0x%02x).\n", temp_pvd->type_code);
        return false;
    }
    if (strncmp(temp_pvd->standard_identifier, "CD001", 5) != 0) {
        fprintf(stderr, "ISO9660 Error: PVD standard identifier is not 'CD001'.\n");
        return false;
    }
    if (temp_pvd->logical_block_size_le != ISO_SECTOR_SIZE) {
        fprintf(stderr, "ISO9660 Error: Logical block size is %d, expected %d.\n",
                temp_pvd->logical_block_size_le, ISO_SECTOR_SIZE);
        return false;
    }

    printf("ISO9660: Valid PVD found. Volume ID: %.*s\n", 32, temp_pvd->volume_identifier);
    memcpy(pvd, temp_pvd, sizeof(IsoPrimaryVolumeDescriptor));

    return true;
}

bool iso_find_file(struct Cdrom* cdrom, IsoDirectoryRecord* directory_record, const char* filename, IsoDirectoryRecord* found_record) {
    uint32_t dir_lba = directory_record->extent_location_le;
    uint32_t dir_size = directory_record->data_length_le;

    printf("ISO9660: Searching for '%s' in directory at LBA %u (size %u bytes)\n", filename, dir_lba, dir_size);

    if (dir_size == 0) {
        fprintf(stderr, "ISO9660 Error: Directory size is 0.\n");
        return false;
    }

    uint8_t sector_buffer[ISO_SECTOR_SIZE];
    uint32_t bytes_searched = 0;

    // This outer loop handles directories that span multiple sectors
    while (bytes_searched < dir_size) {
        if (!read_sector(cdrom, dir_lba, sector_buffer)) {
            fprintf(stderr, "ISO9660 Error: Failed to read directory sector at LBA %u.\n", dir_lba);
            return false;
        }

        // --- CORRECTED LOGIC IS HERE ---
        // This inner loop iterates over all records within the single sector we just read.
        IsoDirectoryRecord* record = (IsoDirectoryRecord*)sector_buffer;
        while ((uint8_t*)record < sector_buffer + ISO_SECTOR_SIZE) {
            // A record length of 0 means we've hit padding at the end of the sector.
            if (record->length == 0) {
                break;
            }

            // Print every filename we find for debugging
            printf("  -> Found entry: '%.*s'\n",
                   record->file_identifier_length,
                   record->file_identifier);

            // Check if the filename matches
            if (record->file_identifier_length == strlen(filename) &&
                strncmp(record->file_identifier, filename, strlen(filename)) == 0)
            {
                printf("ISO9660: Matched file '%s'!\n", filename);
                memcpy(found_record, record, record->length);
                return true; // We found it!
            }

            // Move to the start of the next record
            record = (IsoDirectoryRecord*)((uint8_t*)record + record->length);
        }
        // --- END OF CORRECTION ---

        // We have finished processing this sector, move to the next one
        dir_lba++;
        bytes_searched += ISO_SECTOR_SIZE;
    }

    fprintf(stderr, "ISO9660 Warning: File '%s' not found in directory.\n", filename);
    return false;
}