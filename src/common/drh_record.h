#pragma once

#include "wire_limits.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    bool idr;
    const uint8_t* chunks[DRH_CHUNK_COUNT];
    uint16_t sizes[DRH_CHUNK_COUNT];
} drh_picture_t;

typedef enum {
    DRH_RECORD_OK,
    DRH_RECORD_HEADER_INVALID,
    DRH_RECORD_TRUNCATED,
    DRH_RECORD_CHUNK_INVALID,
    DRH_RECORD_TRAILING_BYTES,
} drh_record_error_t;

static inline uint32_t drh_read_u32(const uint8_t* bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) | ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static inline drh_record_error_t drh_read_picture(const uint8_t* record, size_t available,
                                                  drh_picture_t* picture, size_t* consumed,
                                                  unsigned* invalid_chunk)
{
    if (!record || available < DRH_RECORD_OVERHEAD || record[0] > 1 ||
        drh_read_u32(record + 1) != DRH_CHUNK_COUNT)
        return DRH_RECORD_HEADER_INVALID;

    const size_t length = available < MAX_USB_PAYLOAD ? available : MAX_USB_PAYLOAD;
    picture->idr = record[0] != 0;
    size_t offset = DRH_RECORD_HEADER_SIZE;
    for (unsigned chunk = 0; chunk < DRH_CHUNK_COUNT; ++chunk)
    {
        if (length - offset < 4)
            return DRH_RECORD_TRUNCATED;
        const uint32_t size = drh_read_u32(record + offset);
        offset += 4;
        if (!size || size > MAX_CHUNK_SIZE || size > length - offset)
        {
            if (invalid_chunk)
                *invalid_chunk = chunk;
            return DRH_RECORD_CHUNK_INVALID;
        }
        picture->chunks[chunk] = record + offset;
        picture->sizes[chunk] = (uint16_t)size;
        offset += size;
    }
    *consumed = offset;
    return DRH_RECORD_OK;
}

// A USB record must contain one picture 
static inline drh_record_error_t drh_parse_picture(const uint8_t* record, size_t length,
                                                   drh_picture_t* picture, unsigned* invalid_chunk)
{
    if (length > MAX_USB_PAYLOAD)
        return DRH_RECORD_HEADER_INVALID;
    size_t consumed;
    const drh_record_error_t error =
        drh_read_picture(record, length, picture, &consumed, invalid_chunk);
    if (error != DRH_RECORD_OK)
        return error;
    return consumed == length ? DRH_RECORD_OK : DRH_RECORD_TRAILING_BYTES;
}
