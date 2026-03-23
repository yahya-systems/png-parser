#include "deflate.hpp"
#include "reverse10LUT.hpp"
#include <cstddef>
#include <cstdlib>
#include <string.h>

#define LUT_EMPTY 0x121

unsigned char lengthSymbolValues[] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                      11, 4,  12, 3, 13, 2, 14, 1, 15};

constexpr uint8_t sortedLengthSymbolIndices[20][19] = {
    {},
    {0},
    {0, 1},
    {0, 1, 2},
    {3, 0, 1, 2},
    {3, 4, 0, 1, 2},
    {3, 5, 4, 0, 1, 2},
    {3, 5, 4, 6, 0, 1, 2},
    {3, 7, 5, 4, 6, 0, 1, 2},
    {3, 7, 5, 4, 6, 8, 0, 1, 2},
    {3, 9, 7, 5, 4, 6, 8, 0, 1, 2},
    {3, 9, 7, 5, 4, 6, 8, 10, 0, 1, 2},
    {3, 11, 9, 7, 5, 4, 6, 8, 10, 0, 1, 2},
    {3, 11, 9, 7, 5, 4, 6, 8, 10, 12, 0, 1, 2},
    {3, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 0, 1, 2},
    {3, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 0, 1, 2},
    {3, 15, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 0, 1, 2},
    {3, 15, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 16, 0, 1, 2},
    {3, 17, 15, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 16, 0, 1, 2},
    {3, 17, 15, 13, 11, 9, 7, 5, 4, 6, 8, 10, 12, 14, 16, 18, 0, 1, 2},
};

void generateCodes(uint16_t *bl_count, uint16_t *next_code, uint32_t maxBits) {
  uint16_t code = 0;
  for (uint32_t bits = 1; bits <= maxBits; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }
}

void generateCodes(uint32_t *bl_count, uint32_t *next_code, uint32_t maxBits) {
  uint32_t code = 0;
  for (uint32_t bits = 1; bits <= maxBits; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }
}

void generateCodes(unsigned char *bl_count, unsigned char *next_code,
                   uint32_t maxBits) {
  unsigned char code = 0;
  for (uint32_t bits = 1; bits <= maxBits; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }
}

template <typename T, uint32_t bitSize> class LUT {
  uint32_t length = 1 << bitSize;
  T data[1 << bitSize];
  T defaultValue;

public:
  LUT(const T &defValue) : defaultValue(defValue) {
    for (uint32_t i = 0; i < 1 << bitSize; i++) {
      data[i] = defValue;
    }
  }

  T &operator[](int index) {
    if (index >= length) [[unlikely]] {
      throw "lut buffer overflow";
    }
    return data[index];
  }

  void insertHuffmanCode(uint16_t huffmanCode, uint16_t huffmanCodeLength,
                         T value) {
    if (huffmanCodeLength == 0)
      return;
    if (huffmanCodeLength > bitSize || huffmanCode >= (1 << bitSize)) {
      throw "huffman code too big";
    }

    uint16_t rightOffset = bitSize - huffmanCodeLength;
    huffmanCode = reverse10Table[huffmanCode] >> (10 - huffmanCodeLength);

    if (huffmanCode + (1 << rightOffset) - 1 > 1 << bitSize) {
      throw "huffman code span too big";
    }

    for (uint32_t i = 0; i < (1 << rightOffset); i++) {
      uint16_t index = huffmanCode | (i << huffmanCodeLength);
      if (data[index] != defaultValue) {
        throw "huffman code overlap";
      }
      data[index] = value;
    }
  }
};

inline void memove(unsigned char *dst, const unsigned char *src,
                   uint32_t length) {
  if (src + length > dst) {
    for (uint32_t i = 0; i < length; i++) {

      dst[i] = src[i];
    }
  } else {
    memcpy(dst, src, length);
  }
}

template <uint32_t bitCount>
inline uint16_t peekBits(unsigned char *data, uint32_t bitPtr) {
  // USELESS
}

template <> inline uint16_t peekBits<10>(unsigned char *data, uint32_t bitPtr) {
  uint32_t byteIdx = bitPtr >> 3;
  uint32_t bitIdx = bitPtr & 7;
  uint32_t bytes = (bitIdx <= 6)
                       ? (((uint32_t)data[byteIdx + 1] << 8) | data[byteIdx])
                       : (((uint32_t)data[byteIdx + 2] << 16) |
                          ((uint32_t)data[byteIdx + 1] << 8) | data[byteIdx]);
  return (bytes >> bitIdx) & 0x3FF;
}

template <> inline uint16_t peekBits<5>(unsigned char *data, uint32_t bitPtr) {
  uint32_t byteIdx = bitPtr >> 3;
  uint32_t bitIdx = bitPtr & 7;
  uint16_t two_bytes = ((uint16_t)data[byteIdx + 1] << 8) | data[byteIdx];
  return (two_bytes >> bitIdx) & 0x1F;
}

template <> inline uint16_t peekBits<7>(unsigned char *data, uint32_t bitPtr) {
  uint32_t byteIdx = bitPtr >> 3;
  uint32_t bitIdx = bitPtr & 7;
  uint16_t two_bytes = ((uint16_t)data[byteIdx + 1] << 8) | data[byteIdx];
  return (two_bytes >> bitIdx) & 0x7F;
}

inline uint16_t readBitsReversed(unsigned char *data, uint32_t &bitPtr,
                                 uint32_t bitCount) {
  uint16_t result = 0;
  for (uint32_t i = 0; i < bitCount; i++) {
    uint32_t byteIdx = bitPtr / 8;
    uint32_t bitIdx = bitPtr % 8;
    if (data[byteIdx] & (1 << bitIdx)) {
      result |= (1 << i);
    }
    bitPtr++;
  }
  return result;
}

constexpr uint16_t lengthBase[] = {3,  4,  5,  6,   7,   8,   9,   10,  11, 13,
                                   15, 17, 19, 23,  27,  31,  35,  43,  51, 59,
                                   67, 83, 99, 115, 131, 163, 195, 227, 258};

constexpr uint8_t lengthExtraBits[] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                       1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                       4, 4, 4, 4, 5, 5, 5, 5, 0};

constexpr uint16_t distanceBase[] = {
    1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
    33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
    1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};

constexpr uint8_t distanceExtraBits[] = {0, 0, 0,  0,  1,  1,  2,  2,  3,  3,
                                         4, 4, 5,  5,  6,  6,  7,  7,  8,  8,
                                         9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

namespace deflate {

DResult uncompressDynamicHuffmanBlock(uint32_t &bitPtr, unsigned char *data,
                                      uint32_t length, unsigned char *buffer,
                                      uint32_t &byteIndex,
                                      uint32_t decompressed_data_size);

DResult uncompress(unsigned char *data, uint32_t length, uint8_t *buffer,
                   uint32_t decompressed_data_size) {
  uint32_t bitPtr{};

  // Zlib header info
  uint32_t CMF = readBitsReversed(data, bitPtr, 8);
  uint32_t FLG = readBitsReversed(data, bitPtr, 8);

  if ((CMF * 256 + FLG) % 31 != 0) {
    return ERROR_CORRUPTED_DATA;
  }

  // Decompression
  uint32_t byteIndex{};
  while (true) {
    uint16_t bFinal = readBitsReversed(data, bitPtr, 1);
    uint16_t compressionType = readBitsReversed(data, bitPtr, 2);
    switch (compressionType) {
    case 0: {

      break;
    }
    case 1: {
      break;
    }
    case 2: {
      DResult result = uncompressDynamicHuffmanBlock(
          bitPtr, data, length, buffer, byteIndex, decompressed_data_size);
      if (result != SUCCESS) {
        return result;
      }
    } break;
    }
    if (bFinal) {
      return SUCCESS;
    }
  }
  return ERROR_CORRUPTED_DATA;
}

#define SAFE

DResult uncompressDynamicHuffmanBlock(uint32_t &bitPtr, unsigned char *data,
                                      uint32_t length, unsigned char *buffer,
                                      uint32_t &byteIndex,
                                      uint32_t decompressed_data_size) {
  uint32_t hlit = readBitsReversed(data, bitPtr, 5) + 257;
  uint32_t hdist = readBitsReversed(data, bitPtr, 5) + 1;
  uint32_t hclen = readBitsReversed(data, bitPtr, 4) + 4;

  LUT<uint16_t, 10> literalLUT{LUT_EMPTY};
  unsigned char lengthCodeLengthTable[288]{0};
  LUT<uint16_t, 5> *secondaryLiteralLUTs = nullptr;
  uint16_t secondaryLiteralLUTcount = 0;

  LUT<uint16_t, 10> distanceLUT{LUT_EMPTY};
  unsigned char distanceCodeLengthTable[288]{0};
  LUT<uint16_t, 5> *secondaryDistanceLUTs = nullptr;
  uint16_t secondaryDistanceLUTcount = 0;

  {
    LUT<unsigned char, 7> grandLUT = LUT<unsigned char, 7>{0xFF};
    unsigned char grandLengthTable[19] = {};

    { // grand LUT Construction
      unsigned char bl_count[8] = {};
      unsigned char nextCodes[8] = {};

      for (uint32_t i = 0; i < hclen; i++) {
        unsigned char codeLength = readBitsReversed(data, bitPtr, 3);
        unsigned char code = lengthSymbolValues[i];
        grandLengthTable[code] = codeLength;
        bl_count[codeLength]++;
      }

      bl_count[0] = 0;

      generateCodes(bl_count, nextCodes, 7);

      for (uint32_t i = 0; i < hclen; i++) {
        unsigned char symbol =
            lengthSymbolValues[sortedLengthSymbolIndices[hclen][i]];
        unsigned char huffmanCodeLength = grandLengthTable[symbol];
        unsigned char huffmanCode = nextCodes[huffmanCodeLength]++;

        grandLUT.insertHuffmanCode(huffmanCode, huffmanCodeLength, symbol);
      }
    } // Here, all stuff used to construct grand LUT are gone apart from the LUT
      // itself
    { // Here we construct the Length/Literal + Distance LUTs
      uint16_t literalbl_count[16] = {};
      uint16_t distancebl_count[16] = {};
      uint16_t nextCodes[16] = {};
      uint16_t symbol{};

      unsigned char previousElement{};
      while (symbol < hlit + hdist) {
        uint16_t huffmanCode = peekBits<7>(data, bitPtr);
        unsigned char symbolLength = grandLUT[huffmanCode];

        unsigned char *target = (symbol >= hlit ? distanceCodeLengthTable - hlit
                                                : lengthCodeLengthTable);
        uint16_t *bl_count =
            (symbol >= hlit) ? distancebl_count : literalbl_count;

        if (symbolLength == 0xFF) [[unlikely]] {
          return ERROR_CORRUPTED_DATA;
        }

        bitPtr += grandLengthTable[symbolLength];

        if (symbolLength < 16) {
          target[symbol] = symbolLength;
          bl_count[symbolLength]++;
          symbol++;
          previousElement = symbolLength;
          continue;
        }

        uint32_t copyLength{};
        switch (symbolLength) {
        case 16: {
          if (symbol == 0) [[unlikely]] {
            return ERROR_CORRUPTED_DATA;
          }
          copyLength = readBitsReversed(data, bitPtr, 2) + 3;
          symbolLength = previousElement;
          break;
        }
        case 17: {
          copyLength = readBitsReversed(data, bitPtr, 3) + 3;
          symbolLength = 0;
          break;
        }
        case 18: {
          copyLength = readBitsReversed(data, bitPtr, 7) + 11;
          symbolLength = 0;
          break;
        }
        }

        if (symbol + copyLength > hlit + hdist) [[unlikely]] {
          return ERROR_CORRUPTED_DATA;
        }

        if (symbol >= hlit || copyLength <= hlit - symbol) {
          memset(&target[symbol], symbolLength, copyLength);
          bl_count[symbolLength] += copyLength;
        } else {
          uint16_t literalCopyLength = hlit - symbol;
          uint16_t distanceCopyLength = copyLength - literalCopyLength;
          memset(&lengthCodeLengthTable[symbol], symbolLength,
                 literalCopyLength);
          memset(&distanceCodeLengthTable[symbol - hlit], symbolLength,
                 distanceCopyLength);
          literalbl_count[symbolLength] += literalCopyLength;
          distancebl_count[symbolLength] += distanceCopyLength;
        }

        symbol += copyLength;
        previousElement = symbolLength;
      } // Extracting code lengths for each character

      literalbl_count[0] = 0;

      generateCodes(literalbl_count, nextCodes,
                    15); // We generate the huffman codes Here

      for (uint32_t i = 0; i < hlit; i++) {
        uint16_t symbol = i;
        uint16_t huffmanCodeLength = lengthCodeLengthTable[symbol];
        uint16_t huffmanCode = nextCodes[huffmanCodeLength]++;

        if (huffmanCodeLength <= 10) {
          literalLUT.insertHuffmanCode(huffmanCode, huffmanCodeLength, symbol);
        } else {
          uint16_t huffmanCode10Bit = huffmanCode >> (huffmanCodeLength - 10);

          uint16_t huffmanCode5BitLength = huffmanCodeLength - 10;
          uint16_t huffmanCode5Bit =
              huffmanCode & ((1 << huffmanCode5BitLength) - 1);

          if (literalLUT[reverse10Table[huffmanCode10Bit]] == LUT_EMPTY) {
            LUT<uint16_t, 5> *temp = (LUT<uint16_t, 5> *)realloc(
                secondaryLiteralLUTs,
                ++secondaryLiteralLUTcount * sizeof(LUT<uint16_t, 5>));

            if (!temp) [[unlikely]] {
              return ERROR_OUT_OF_MEMORY; // Your system is completely cooked
            }

            secondaryLiteralLUTs = temp;

            secondaryLiteralLUTs[secondaryLiteralLUTcount - 1] =
                LUT<uint16_t, 5>{LUT_EMPTY};

            literalLUT.insertHuffmanCode(huffmanCode10Bit, 10,
                                         secondaryLiteralLUTcount + LUT_EMPTY);
          }

          if (literalLUT[reverse10Table[huffmanCode10Bit]] < LUT_EMPTY)
              [[unlikely]] {
            return ERROR_CORRUPTED_DATA;
          }

          secondaryLiteralLUTs[literalLUT[reverse10Table[huffmanCode10Bit]] -
                               LUT_EMPTY - 1]
              .insertHuffmanCode(huffmanCode5Bit, huffmanCode5BitLength,
                                 symbol);
        }
      }

      memset(nextCodes, 0, 32);

      distancebl_count[0] = 0;

      generateCodes(distancebl_count, nextCodes, 15);

      for (uint32_t i = 0; i < hdist; i++) {
        uint16_t symbol = i;
        uint16_t huffmanCodeLength = distanceCodeLengthTable[symbol];
        uint16_t huffmanCode = nextCodes[huffmanCodeLength]++;

        if (huffmanCodeLength <= 10) {
          distanceLUT.insertHuffmanCode(huffmanCode, huffmanCodeLength, symbol);
        } else {
          uint16_t huffmanCode10Bit = huffmanCode >> (huffmanCodeLength - 10);
          uint16_t huffmanCode5BitLength = huffmanCodeLength - 10;
          uint16_t huffmanCode5Bit =
              huffmanCode & ((1 << huffmanCode5BitLength) - 1);

          if (distanceLUT[reverse10Table[huffmanCode10Bit]] == LUT_EMPTY) {
            LUT<uint16_t, 5> *temp = (LUT<uint16_t, 5> *)realloc(
                secondaryDistanceLUTs,
                ++secondaryDistanceLUTcount * sizeof(LUT<uint16_t, 5>));

            if (!temp) [[unlikely]] {
              return ERROR_OUT_OF_MEMORY;
            }

            secondaryDistanceLUTs = temp;
            secondaryDistanceLUTs[secondaryDistanceLUTcount - 1] =
                LUT<uint16_t, 5>{LUT_EMPTY};

            distanceLUT.insertHuffmanCode(
                huffmanCode10Bit, 10, secondaryDistanceLUTcount + LUT_EMPTY);
          }

          if (distanceLUT[reverse10Table[huffmanCode10Bit]] < LUT_EMPTY)
              [[unlikely]] {
            return ERROR_CORRUPTED_DATA;
          }

          secondaryDistanceLUTs[distanceLUT[reverse10Table[huffmanCode10Bit]] -
                                LUT_EMPTY - 1]
              .insertHuffmanCode(huffmanCode5Bit, huffmanCode5BitLength,
                                 symbol);
        }
      }
    } // Here, we complete constructing the literal and distance LUTs
  } // Here, we destroy the grand LUT

  while (true) {
#ifdef SAFE
    if (bitPtr > length * 8 || byteIndex > decompressed_data_size)
        [[unlikely]] {
      return ERROR_OUT_OF_MEMORY;
    }
#endif
    uint16_t huffmanCode = peekBits<10>(data, bitPtr);
    uint16_t symbol = literalLUT[huffmanCode];
    if (symbol > LUT_EMPTY) {
      uint16_t huffmanCode5Bit = peekBits<5>(data, bitPtr + 10);
      uint16_t tableIndex = symbol - LUT_EMPTY - 1;
      symbol = secondaryLiteralLUTs[tableIndex][huffmanCode5Bit];
    }

#ifdef SAFE
    if (symbol == LUT_EMPTY) [[unlikely]] {
      return ERROR_CORRUPTED_DATA;
    }
#endif

    bitPtr += lengthCodeLengthTable[symbol];

    if (symbol == 256) {
      free(secondaryLiteralLUTs);
      free(secondaryDistanceLUTs);
      return SUCCESS;
    }

    if (symbol < 256) {
      buffer[byteIndex++] = symbol;
      continue;
    }

    // In this case symbol > 256
    uint16_t copyLength =
        lengthBase[symbol - 257] +
        readBitsReversed(data, bitPtr, lengthExtraBits[symbol - 257]);

    huffmanCode = peekBits<10>(data, bitPtr);
    symbol = distanceLUT[huffmanCode];
    if (symbol > LUT_EMPTY) {
      uint16_t huffmanCode5Bit = peekBits<5>(data, bitPtr + 10);
      uint16_t tableIndex = symbol - LUT_EMPTY - 1;
      symbol = secondaryDistanceLUTs[tableIndex][huffmanCode5Bit];
    }

#ifdef SAFE
    if (symbol == LUT_EMPTY) [[unlikely]] {
      return ERROR_CORRUPTED_DATA;
    }
#endif

    bitPtr += distanceCodeLengthTable[symbol];

#ifdef SAFE
    if (symbol >= 30) [[unlikely]] {
      return ERROR_CORRUPTED_DATA;
    }
#endif

    uint16_t distance =
        distanceBase[symbol] +
        readBitsReversed(data, bitPtr, distanceExtraBits[symbol]);
#ifdef SAFE
    if (byteIndex < distance) [[unlikely]] {
      return ERROR_CORRUPTED_DATA;
    }
#endif

    memove(&buffer[byteIndex], &buffer[byteIndex - distance], copyLength);
    byteIndex += copyLength;
  }
}

} // namespace deflate
//
