#ifndef FIRMWARE_METADATA_H
#define FIRMWARE_METADATA_H

#define FW_META_OFFSET 0x040C

typedef struct FirmwareMetadata
{
    uint8_t major;
    uint8_t minor;
    uint8_t rev;
    uint8_t _reserved0;
    uint32_t _reserved1;
    uint32_t _reserved2;
} __attribute__((packed)) FirmwareMetadata_t;

#endif // FIRMWARE_METADATA_H
