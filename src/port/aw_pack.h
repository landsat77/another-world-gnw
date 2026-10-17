#ifndef AW_PACK_H
#define AW_PACK_H

#include <stdint.h>
#include <stdbool.h>

#define AW_PACK_MAGIC "GWAW01\0\0"

enum {
    AW_PACK_OK = 0,
    AW_PACK_ERR_MAGIC = -1,
    AW_PACK_ERR_CORRUPT = -2,
};

int aw_pack_open(const uint8_t *data, uint32_t size);
bool aw_pack_find(const char *name, const uint8_t **data, uint32_t *size);
uint32_t aw_pack_count(void);
const char *aw_pack_name(uint32_t index);

#endif
