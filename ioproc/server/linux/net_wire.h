/* Little-endian 32-bit words and byte fields in Helios socket control records.
 * Record decoders check the complete length before reading any fields.
 * Native C structures are never overlaid on a message buffer. */
#ifndef IOS_NET_WIRE_H
#define IOS_NET_WIRE_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

typedef struct {
    const unsigned char *next;
} WireReader;

typedef struct {
    unsigned char *next;
} WireWriter;

static inline uint32_t wire_read_word(WireReader *reader)
{
    const unsigned char *bytes = reader->next;
    uint32_t value = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
                     (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;

    reader->next += 4;
    return value;
}

static inline void wire_read_bytes(WireReader *reader, void *data, size_t size)
{
    memcpy(data, reader->next, size);
    reader->next += size;
}

static inline void wire_write_word(WireWriter *writer, uint32_t value)
{
    unsigned char *bytes = writer->next;

    bytes[0] = value;
    bytes[1] = value >> 8;
    bytes[2] = value >> 16;
    bytes[3] = value >> 24;
    writer->next += 4;
}

static inline void wire_write_bytes(WireWriter *writer, const void *data, size_t size)
{
    memcpy(writer->next, data, size);
    writer->next += size;
}

#endif
