#ifndef IMAGE_PROCESSOR
#define IMAGE_PROCESSOR
#include <cstdint>

enum CHANNEL {
  NONE = 0,
  RGBA = 4,
  RGB = 3,
  G = 1,
  GA = 2,
};

uint8_t *openImage(const char *image, uint32_t *width, uint32_t *height,
                   CHANNEL *imageChannel, CHANNEL outputChannel,
                   bool flip = false);

#endif
