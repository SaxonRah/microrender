#include "gfx_pack.h"
#include <string.h>

static uint16_t rd16(const unsigned char *p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const unsigned char *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

int gfx_pack_open(gfx_pack_t GFX_PTR *pack, const char *path) {
  unsigned char header[12];
  uint16_t count;
  uint32_t data_offset;
  uint16_t i;

  if (!pack || !path)
    return 0;
  memset(pack, 0, sizeof(*pack));
  pack->fp = fopen(path, "rb");
  if (!pack->fp)
    return 0;

  if (fread(header, 1, sizeof(header), pack->fp) != sizeof(header)) {
    gfx_pack_close(pack);
    return 0;
  }
  if (header[0] != GFX_PACK_MAGIC0 || header[1] != GFX_PACK_MAGIC1 ||
      header[2] != GFX_PACK_MAGIC2 || header[3] != GFX_PACK_MAGIC3) {
    gfx_pack_close(pack);
    return 0;
  }

  count = rd16(header + 4);
  data_offset = rd32(header + 8);
  if (count > GFX_PACK_MAX_ENTRIES) {
    gfx_pack_close(pack);
    return 0;
  }
  if (data_offset <
      (uint32_t)(GFX_PACK_HEADER_SIZE +
                 ((uint32_t)count * (uint32_t)GFX_PACK_DIR_ENTRY_SIZE))) {
    gfx_pack_close(pack);
    return 0;
  }

  pack->count = count;
  pack->data_offset = data_offset;

  for (i = 0; i < count; ++i) {
    unsigned char dir[GFX_PACK_DIR_ENTRY_SIZE];
    unsigned int n;
    if (fread(dir, 1, sizeof(dir), pack->fp) != sizeof(dir)) {
      gfx_pack_close(pack);
      return 0;
    }
    n = dir[0];
    if (n > 31u)
      n = 31u;
    memset(pack->entries[i].name, 0, sizeof(pack->entries[i].name));
    memcpy(pack->entries[i].name, dir + 1, n);
    pack->entries[i].kind = rd16(dir + 32);
    pack->entries[i].offset = rd32(dir + 36);
    pack->entries[i].size = rd32(dir + 40);
  }

  return 1;
}

void gfx_pack_close(gfx_pack_t GFX_PTR *pack) {
  if (!pack)
    return;
  if (pack->fp)
    fclose(pack->fp);
  memset(pack, 0, sizeof(*pack));
}

const gfx_pack_entry_t GFX_PTR *gfx_pack_find(const gfx_pack_t GFX_PTR *pack,
                                              const char *name) {
  uint16_t i;
  if (!pack || !name)
    return 0;
  for (i = 0; i < pack->count; ++i) {
    if (strcmp(pack->entries[i].name, name) == 0)
      return &pack->entries[i];
  }
  return 0;
}

const char *gfx_pack_kind_name(uint16_t kind) {
  switch (kind) {
  case GFX_PACK_ENTRY_SPRITE_RAW:
    return "sprite_raw";
  case GFX_PACK_ENTRY_SPRITE_RLE:
    return "sprite_rle";
  case GFX_PACK_ENTRY_TILEMAP_U16:
    return "tilemap_u16";
  case GFX_PACK_ENTRY_PALETTE_RGB:
    return "palette_rgb";
  case GFX_PACK_ENTRY_ANIM:
    return "anim";
  case GFX_PACK_ENTRY_COLLISION_U8:
    return "collision_u8";
  case GFX_PACK_ENTRY_SPAWNS:
    return "spawns";
  case GFX_PACK_ENTRY_TRIGGERS:
    return "triggers";
  case GFX_PACK_ENTRY_TILE_FLAGS:
    return "tile_flags";
  case GFX_PACK_ENTRY_AUDIO_U8:
    return "audio_u8";
  case GFX_PACK_ENTRY_PROJECT_INFO:
    return "project_info";
  default:
    return "unknown";
  }
}

int gfx_pack_read(const gfx_pack_t GFX_PTR *pack,
                  const gfx_pack_entry_t GFX_PTR *entry, void GFX_PTR *dst,
                  uint32_t max_bytes, uint32_t GFX_PTR *out_bytes) {
  uint32_t n;
  if (!pack || !pack->fp || !entry || !dst)
    return 0;
  n = entry->size;
  if (n > max_bytes)
    n = max_bytes;
  if (fseek(pack->fp, (long)(pack->data_offset + entry->offset), SEEK_SET) != 0)
    return 0;
  if (fread(dst, 1, (size_t)n, pack->fp) != (size_t)n)
    return 0;
  if (out_bytes)
    *out_bytes = n;
  return 1;
}
