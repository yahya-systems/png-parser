#include "png-parser/image-processor.h"
#include "deflate/deflate.hpp"
#include <cstdio>

#include <cstring>
#include <ctime>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zconf.h>

bool endsWith(const char *filename, const char *suffix) {
  size_t lenFile = strlen(filename);
  size_t lenSuffix = strlen(suffix);

  if (lenSuffix > lenFile)
    return false;

  return strcmp(filename + lenFile - lenSuffix, suffix) == 0;
}

inline void readBytes(void *ptr, size_t size, size_t count, FILE *file) {
  size_t n = fread(ptr, size, count, file);
  if (n != count) {
    fprintf(stderr, "Error: expected %zu elements but read %zu\n", count, n);
    exit(EXIT_FAILURE);
  }
}

inline uint16_t swap_uint16(uint16_t val) { return (val >> 8) | (val << 8); }

inline uint32_t swap_uint32(uint32_t val) {
  return ((val >> 24) & 0xFF) | ((val >> 8) & 0xFF00) |
         ((val << 8) & 0xFF0000) | ((val << 24) & 0xFF000000);
}

uint32_t mulExcess(uint32_t num, uint32_t n) {
  if (n == 0)
    return 0; // avoid division by zero
  return ((num + n - 1) / n) * n;
}

struct RGBA {
  uint32_t r, g, b, a;
};

struct RGB {
  uint32_t r, g, b;
};

struct Header {
  char signature[2];
  uint32_t fileSize;
  uint32_t reserved;
  uint32_t offset;
};

struct HeaderInfo {
  uint32_t size;
  uint32_t width;
  uint32_t height;
  uint16_t planes;
  uint16_t bitsPerPixel;
  uint32_t compression;
  uint32_t imgaeSize;
  uint32_t XpixelPerM;
  uint32_t YpixelPerM;
  uint32_t colorsUsed;
  uint32_t importantColors;
};

uint32_t jumpToChunk(uint8_t *chunk, FILE *file) {
  while (true) {
    uint32_t len = 0;
    if (fread(&len, 4, 1, file) != 1) {
      return 1;
    }
    len = swap_uint32(len);
    uint8_t seg[4];
    if (fread(seg, 4, 1, file) != 1) {
      return 1;
    }
    uint8_t END_CHUNK[] = {0x49, 0x45, 0x4E, 0x44};
    if (memcmp(seg, END_CHUNK, 4) == 0) {
      return 1;
    }
    if (memcmp(seg, chunk, 4) != 0) {
      fseek(file, len + 4, SEEK_CUR);
      continue;
    }

    fseek(file, -8, SEEK_CUR);
    return 0;
  }
}

uint32_t paethPreditor(int32_t a, int32_t b, int32_t c) {
  int32_t p = a + b - c;
  int32_t pa = abs(p - a);
  int32_t pb = abs(p - b);
  int32_t pc = abs(p - c);
  if (pa <= pb && pa <= pc) {
    return a;
  }
  if (pb <= pc) {
    return b;
  }
  return c;
}

CHANNEL pngColorTypeToChannel(uint8_t pngColorType) {
  switch (pngColorType) {
  case 0:
    return G;
  case 2:
    return RGB;
  case 4:
    return GA;
  case 6:
    return RGBA;
  default:
    return NONE;
  }
}

uint32_t filter_png8(unsigned char *uncompressed_data, uint32_t width,
                     uint32_t height, uint32_t channels) {
  uint32_t bytesPerRow = width * channels;
  uint32_t rowWithFilter = 1 + bytesPerRow;
  for (uint32_t i = 0; i < height; i++) {
    unsigned char filterType = uncompressed_data[i * rowWithFilter];
    switch (filterType) {
    case 0: {
      continue;
    }
    case 1: {
      for (uint32_t j = 1; j < width; j++) {
        for (uint32_t k = 0; k < channels; k++) {
          uncompressed_data[i * rowWithFilter + j * channels + k + 1] +=
              uncompressed_data[i * rowWithFilter + (j - 1) * channels + k + 1];
        }
      }
      break;
    }
    case 2: {
      for (uint32_t j = 0; j < width; j++) {
        for (uint32_t k = 0; k < channels; k++) {
          uncompressed_data[i * rowWithFilter + j * channels + k + 1] +=
              (i == 0) ? 0
                       : uncompressed_data[(i - 1) * rowWithFilter +
                                           j * channels + k + 1];
        }
      }
      break;
    }
    case 3: {
      for (uint32_t k = 0; k < channels; k++) {
        uncompressed_data[i * rowWithFilter + k + 1] += floor(
            ((i == 0) ? 0
                      : uncompressed_data[(i - 1) * rowWithFilter + k + 1]) /
            2);
      }
      for (uint32_t j = 1; j < width; j++) {
        for (uint32_t k = 0; k < channels; k++) {
          uncompressed_data[i * rowWithFilter + j * channels + k + 1] +=
              floor((uncompressed_data[i * rowWithFilter + (j - 1) * channels +
                                       k + 1] +
                     ((i == 0) ? 0
                               : uncompressed_data[(i - 1) * rowWithFilter +
                                                   j * channels + k + 1])) /
                    2);
        }
      }
      break;
    }
    case 4: {
      for (uint32_t k = 0; k < channels; k++) {
        uncompressed_data[i * rowWithFilter + k + 1] += paethPreditor(
            0,
            (int32_t)(i == 0
                          ? 0
                          : uncompressed_data[(i - 1) * rowWithFilter + k + 1]),
            0);
      }
      for (uint32_t j = 1; j < width; j++) {
        for (uint32_t k = 0; k < channels; k++) {
          uncompressed_data[i * rowWithFilter + j * channels + k + 1] +=
              paethPreditor(
                  (int32_t)uncompressed_data[i * rowWithFilter +
                                             (j - 1) * channels + k + 1],
                  (int32_t)(i == 0 ? 0
                                   : uncompressed_data[(i - 1) * rowWithFilter +
                                                       j * channels + k + 1]),
                  (int32_t)(i == 0 ? 0
                                   : uncompressed_data[(i - 1) * rowWithFilter +
                                                       (j - 1) * channels + k +
                                                       1]));
        }
      }
      break;
    }
    }
  }
  return 0;
}

uint32_t convertColorType(CHANNEL inputChannel,
                          unsigned char *uncompressed_data,
                          CHANNEL outputChannel, unsigned char *imageData,
                          uint32_t width, uint32_t height) {
  switch (inputChannel) {
  case RGB: {
    uint32_t rowWithFilter = 1 + width * 3;
    switch (outputChannel) {
    case RGB: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 3 + j * 3] =
              uncompressed_data[i * rowWithFilter + j * 3 + 1];
          imageData[i * width * 3 + j * 3 + 1] =
              uncompressed_data[i * rowWithFilter + j * 3 + 1 + 1];
          imageData[i * width * 3 + j * 3 + 2] =
              uncompressed_data[i * rowWithFilter + j * 3 + 2 + 1];
        }
      }
      return 0;
    }
    case RGBA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 4 + j * 4] =
              uncompressed_data[i * rowWithFilter + j * 3 + 1];
          imageData[i * width * 4 + j * 4 + 1] =
              uncompressed_data[i * rowWithFilter + j * 3 + 1 + 1];
          imageData[i * width * 4 + j * 4 + 2] =
              uncompressed_data[i * rowWithFilter + j * 3 + 2 + 1];
          imageData[i * width * 4 + j * 4 + 3] = 255;
        }
      }
      return 0;
    }
    case G: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          uint32_t outputColor =
              (uncompressed_data[i * rowWithFilter + j * 3 + 1] +
               uncompressed_data[i * rowWithFilter + j * 3 + 1 + 1] +
               uncompressed_data[i * rowWithFilter + j * 3 + 2 + 1]) /
              3;
          imageData[i * width + j] = outputColor;
        }
      }
      return 0;
    }
    case GA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          uint32_t outputColor =
              (uncompressed_data[i * rowWithFilter + j * 3 + 1] +
               uncompressed_data[i * rowWithFilter + j * 3 + 1 + 1] +
               uncompressed_data[i * rowWithFilter + j * 3 + 2 + 1]) /
              3;
          imageData[i * width * 2 + j * 2] = outputColor;
          imageData[i * width * 2 + j * 2 + 1] = 255;
        }
      }
      return 0;
    }
    default:
      return 0;
    }
  }
  case RGBA: {
    uint32_t rowWithFilter = 1 + width * 4;
    switch (outputChannel) {
    case RGB: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 3 + j * 3] =
              uncompressed_data[i * rowWithFilter + j * 4 + 1];
          imageData[i * width * 3 + j * 3 + 1] =
              uncompressed_data[i * rowWithFilter + j * 4 + 1 + 1];
          imageData[i * width * 3 + j * 3 + 2] =
              uncompressed_data[i * rowWithFilter + j * 4 + 2 + 1];
          // We Neglect The Alpha Channel
        }
      }
      return 0;
    }
    case RGBA: {
      // We Remove The Filter Byte
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 4 + j * 4] =
              uncompressed_data[i * rowWithFilter + j * 4 + 1];
          imageData[i * width * 4 + j * 4 + 1] =
              uncompressed_data[i * rowWithFilter + j * 4 + 1 + 1];
          imageData[i * width * 4 + j * 4 + 2] =
              uncompressed_data[i * rowWithFilter + j * 4 + 2 + 1];
          imageData[i * width * 4 + j * 4 + 3] =
              uncompressed_data[i * rowWithFilter + j * 4 + 3 + 1];
        }
      }
      return 0;
    }
    case G:
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          uint32_t outputValue =
              (uncompressed_data[i * rowWithFilter + j * 4 + 1] +
               uncompressed_data[i * rowWithFilter + j * 4 + 1 + 1] +
               uncompressed_data[i * rowWithFilter + j * 4 + 2 + 1]) /
              3;
          imageData[i * width + j] = outputValue;
        }
      }
      return 0;
    case GA:
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          uint32_t outputValue =
              (uncompressed_data[i * rowWithFilter + j * 4 + 1] +
               uncompressed_data[i * rowWithFilter + j * 4 + 1 + 1] +
               uncompressed_data[i * rowWithFilter + j * 4 + 2 + 1]) /
              3;
          imageData[i * width * 2 + j] = outputValue;
          imageData[i * width * 2 + j + 1] =
              uncompressed_data[i * rowWithFilter + j * 4 + 3 + 1];
        }
      }
      return 0;
    default:
      return 0;
    }
  }
  case G: {
    uint32_t rowWithFilter = 1 + width;
    switch (outputChannel) {
    case RGB: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 3 + j * 3] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 3 + j * 3 + 1] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 3 + j * 3 + 2] =
              uncompressed_data[i * rowWithFilter + j + 1];
        }
      }
      return 0;
    }
    case RGBA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 4 + j * 4] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 4 + j * 4 + 1] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 4 + j * 4 + 2] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 4 + j * 4 + 3] = 255;
        }
      }
      return 0;
    }
    case G: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width + j] =
              uncompressed_data[i * rowWithFilter + j + 1];
        }
      }
      return 0;
    }
    case GA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 2 + j * 2] =
              uncompressed_data[i * rowWithFilter + j + 1];
          imageData[i * width * 2 + j * 2 + 1] = 255;
        }
      }
      return 0;
    }
    default:
      return 0;
    }
  }
  case GA: {
    uint32_t rowWithFilter = 1 + width * 2;
    switch (outputChannel) {
    case RGB: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 3 + j * 3] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 3 + j * 3 + 1] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 3 + j * 3 + 2] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
        }
      }
      // We Neglect The Alpha Value
      return 0;
    }
    case RGBA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 4 + j * 4] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 4 + j * 4 + 1] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 4 + j * 4 + 2] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 4 + j * 4 + 3] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1 + 1];
        }
      }
      return 0;
    }
    case G: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width + j] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
        }
      }
      return 0;
    }
    case GA: {
      for (uint32_t i = 0; i < height; i++) {
        for (uint32_t j = 0; j < width; j++) {
          imageData[i * width * 2 + j * 2] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1];
          imageData[i * width * 2 + j * 2 + 1] =
              uncompressed_data[i * rowWithFilter + j * 2 + 1 + 1];
        }
      }
      return 0;
    }
    default:
      return 0;
    }
  }
  default:
    return 0;
  }
  return 0;
}

uint32_t decode_PNG8(unsigned char *compressed_data,
                     uint32_t compressed_data_length, uint32_t width,
                     uint32_t height, uint32_t colorType, CHANNEL outputChannel,
                     unsigned char **imageData, uint32_t *image_data_length,
                     CHANNEL *inputChannel) {
  uint32_t rowBytes = width * pngColorTypeToChannel(colorType);
  uint32_t rowWithFilter = 1 + rowBytes;
  uint32_t totalInflalted = height * rowWithFilter;
  unsigned char *uncompressed_data = (unsigned char *)malloc(totalInflalted);

  uint32_t result = deflate::uncompress(compressed_data, compressed_data_length,
                                        uncompressed_data, totalInflalted);
  if (result != deflate::SUCCESS) {
    return result;
  }

  CHANNEL imageChannel = pngColorTypeToChannel(colorType);
  if (inputChannel) {
    *inputChannel = imageChannel;
  }
  filter_png8(uncompressed_data, width, height, imageChannel);
  if (outputChannel != NONE) {
    *imageData = (unsigned char *)malloc(width * height * outputChannel);
    convertColorType(imageChannel, uncompressed_data, outputChannel, *imageData,
                     width, height);
  } else {
    *imageData = (unsigned char *)malloc(width * height * imageChannel);
    convertColorType(imageChannel, uncompressed_data, imageChannel, *imageData,
                     width, height);
  }
  free(uncompressed_data);
  free(compressed_data);
  return 0;
}

unsigned char *openImagePNG(const char *imageURL, uint32_t *width,
                            uint32_t *height, CHANNEL *imageChannel,
                            CHANNEL outputChannel, bool flip) {
  FILE *file;
  file = fopen(imageURL, "rb");
  if (!file) {
    return nullptr;
  }

  uint8_t PNG_SIGNATURE[] = {0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};

  uint8_t sig[8];
  readBytes(sig, 1, 8, file);

  if (memcmp(sig, PNG_SIGNATURE, 8) != 0) {
    return nullptr;
  }

  uint8_t IHDR_CHUNK[] = {0x49, 0x48, 0x44, 0x52};

  uint32_t idhr_chunk_size;
  readBytes(&idhr_chunk_size, 4, 1, file);
  idhr_chunk_size = swap_uint32(idhr_chunk_size);

  readBytes(sig, 4, 1, file);

  if (memcmp(sig, IHDR_CHUNK, 4) != 0) {
    return nullptr;
  }

  uint32_t dimensions[2];
  readBytes(&dimensions, 4, 2, file);
  dimensions[0] = swap_uint32(dimensions[0]);
  dimensions[1] = swap_uint32(dimensions[1]);

  *width = dimensions[0];
  *height = dimensions[1];

  uint8_t imageProperties[5];
  readBytes(imageProperties, 5, 1, file);

  if (imageProperties[2] != 0) {
    return nullptr;
  }

  if (imageProperties[3] != 0) {
    return nullptr;
  }

  if (imageProperties[4] != 0) {
    return nullptr;
  }

  fseek(file, 4, SEEK_CUR); // skipping CRC

  uint8_t IDAT_CHUNK[] = {0x49, 0x44, 0x41, 0x54};
  long cur_pos = ftell(file);
  uint32_t totalLength = 0;
  while (!jumpToChunk(IDAT_CHUNK, file)) {
    uint32_t len = 0;
    readBytes(&len, 4, 1, file);
    len = swap_uint32(len);
    totalLength += len;
    fseek(file, len + 8, SEEK_CUR);
  }

  fseek(file, cur_pos, SEEK_SET);

  uint8_t *data = (uint8_t *)malloc(totalLength);
  uint8_t *iterator = data;
  while (!jumpToChunk(IDAT_CHUNK, file)) {
    uint32_t len = 0;
    readBytes(&len, 4, 1, file);
    len = swap_uint32(len);
    fseek(file, 4, SEEK_CUR);
    readBytes(iterator, len, 1, file);
    iterator += len;
    fseek(file, 4, SEEK_CUR);
  }

  unsigned char *imageData = nullptr;
  uint32_t imageDataLength = 0;

  if (imageProperties[0] == 8) {
    uint32_t result = decode_PNG8(
        data, totalLength, dimensions[0], dimensions[1], imageProperties[1],
        outputChannel, &imageData, &imageDataLength, imageChannel);
    if (result != 0) {
      return nullptr;
    }
  }

  if (flip) {
    uint8_t *swapbuffer = (uint8_t *)malloc(dimensions[0] * 4);
    for (uint32_t i = 0; i < dimensions[1] / 2; i++) {
      memcpy(swapbuffer,
             &imageData[(dimensions[1] - 1 - i) * dimensions[0] * 4],
             dimensions[0] * 4);
      memcpy(&imageData[(dimensions[1] - 1 - i) * dimensions[0] * 4],
             &imageData[i * dimensions[0] * 4], dimensions[0] * 4);
      memcpy(&imageData[i * dimensions[0] * 4], swapbuffer, dimensions[0] * 4);
    }
    free(swapbuffer);
  }

  fclose(file);
  return imageData;
}

unsigned char *openImageBMP(const char *imageURL, uint32_t *width,
                            uint32_t *height, CHANNEL outputChannel,
                            bool flip) {
  FILE *file;
  file = fopen(imageURL, "rb");
  if (!file) {
    return nullptr;
  }

  Header header;
  fread(&header.signature, 2, 1, file);
  fread(&header.fileSize, 4, 3, file);

  HeaderInfo headerInfo;
  fread(&headerInfo, sizeof(HeaderInfo), 1, file);

  fseek(file, header.offset, SEEK_SET);

  if (headerInfo.bitsPerPixel != 24) {
    return nullptr;
  }
  if (headerInfo.compression != 0) {
    return nullptr;
  }

  *width = headerInfo.width;
  if (headerInfo.height < 0) {
    flip = !flip;
    *height = headerInfo.height * -1;
  } else {
    *height = headerInfo.height;
  }
  uint32_t byteWidth = headerInfo.width * 3; // 3 bytes per pixel
  uint32_t padding = mulExcess(byteWidth, 4) - byteWidth;
  uint32_t size = *height * byteWidth;
  unsigned char *imageData = (unsigned char *)malloc(size);

  if (!imageData) {
    return nullptr;
  }
  if (flip) {
    for (uint32_t i = 0; i < *height; i++) {
      unsigned char *rowPtr = imageData + i * byteWidth;
      if (fread(rowPtr, 1, byteWidth, file) != byteWidth) {
        return nullptr;
      }
      fseek(file, padding, SEEK_CUR);
    }
  } else {
    for (uint32_t i = 0; i < *height; i++) {
      unsigned char *rowPtr = imageData + size - byteWidth - i * byteWidth;
      if (fread(rowPtr, 1, byteWidth, file) != byteWidth) {
        free(imageData);
        return nullptr;
      }
      fseek(file, padding, SEEK_CUR);
    }
  }

  switch (outputChannel) {
  case RGB: {
    for (uint32_t i = 0; i < size; i += 3) {
      unsigned char temp = imageData[i];
      imageData[i] = imageData[i + 2];
      imageData[i + 2] = temp;
    }
    fclose(file);
    return imageData;
  }
  case RGBA: {
    uint32_t newSize = headerInfo.width * *height * 4;
    unsigned char *temp = (unsigned char *)malloc(newSize);
    if (!temp) {
      free(imageData);
      return nullptr;
    }
    for (uint32_t i = 0; i < headerInfo.width * *height; i++) {
      temp[4 * i] = imageData[3 * i + 2];
      temp[4 * i + 1] = imageData[3 * i + 1];
      temp[4 * i + 2] = imageData[3 * i];
      temp[4 * i + 3] = 255;
    }
    free(imageData);
    fclose(file);
    return temp;
  }
  case G: {
    uint32_t newSize = headerInfo.width * *height;
    unsigned char *temp = (unsigned char *)malloc(newSize);
    if (!temp) {
      free(imageData);
      return nullptr;
    }
    for (uint32_t i = 0; i < newSize; i++) {
      unsigned char color =
          (imageData[3 * i] + imageData[3 * i + 1] + imageData[3 * i + 2]) / 3;
      temp[i] = color;
    }
    free(imageData);
    fclose(file);
    return temp;
  }
  default:
    break;
  }
  return nullptr;
}

uint8_t *openImage(const char *image, uint32_t *width, uint32_t *height,
                   CHANNEL *imageChannel, CHANNEL outputChannel, bool flip) {
  if (endsWith(image, ".bmp")) {
    return openImageBMP(image, width, height, outputChannel, flip);
  }
  if (endsWith(image, ".png")) {
    return openImagePNG(image, width, height, imageChannel, outputChannel,
                        flip);
  }
  return nullptr;
}
