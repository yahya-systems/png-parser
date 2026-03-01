#include "deflate.h"
#include <stdio.h>
#include <string.h>

namespace deflate {

uint32_t read_bits(const uint8_t *data, uint32_t &bitPtr, int numBits) {
  uint32_t result = 0;
  for (int i = 0; i < numBits; i++) {
    // Find which byte we are in
    uint32_t byteIdx = bitPtr / 8;
    // Find which bit within that byte (0-7)
    uint32_t bitIdx = bitPtr % 8;

    // Extract the bit (LSB first)
    if (data[byteIdx] & (1 << bitIdx)) {
      result |= (1 << i);
    }

    bitPtr++; // Move to the next bit
  }
  return result;
}

class HuffmanNode {
public:
  uint16_t symbol;
  bool is_leaf;
  HuffmanNode *left;  // bit 0
  HuffmanNode *right; // bit 1

  HuffmanNode() : symbol('\0'), is_leaf(false), left(nullptr), right(nullptr) {}
};

class HuffmanTree {
public:
  enum Result {
    OK,
    ERR_NULL_BUFFER,
    ERR_ZERO_BITS,
    ERR_PREFIX_CONFLICT,
    ERR_OVERWRITE_BRANCH,
    ERR_INVALID_BIT,
  };

  HuffmanTree() : cursor(nullptr) { root = new HuffmanNode(); }

  ~HuffmanTree() { destroy(root); }

  Result create_branch(const char *buf, int bit_count, uint16_t value) {
    if (!buf)
      return ERR_NULL_BUFFER;
    if (bit_count <= 0)
      return ERR_ZERO_BITS;

    HuffmanNode *cur = root;

    for (int i = 0; i < bit_count; i++) {
      if (cur->is_leaf)
        return ERR_PREFIX_CONFLICT;

      int bit = extract_bit(buf, i);

      HuffmanNode **child = bit ? &cur->right : &cur->left;
      if (!*child)
        *child = new HuffmanNode();

      cur = *child;
    }

    if (cur->left || cur->right)
      return ERR_OVERWRITE_BRANCH;

    cur->symbol = value;
    cur->is_leaf = true;
    return OK;
  }

  Result create_branch_from_code(uint32_t code, int bit_count, uint16_t value) {
    if (bit_count <= 0)
      return ERR_ZERO_BITS;

    HuffmanNode *cur = root;

    for (int i = bit_count - 1; i >= 0;
         i--) { // MSB of code = first bit to walk
      if (cur->is_leaf)
        return ERR_PREFIX_CONFLICT;

      int bit = (code >> i) & 1;

      HuffmanNode **child = bit ? &cur->right : &cur->left;
      if (!*child)
        *child = new HuffmanNode();

      cur = *child;
    }

    if (cur->left || cur->right)
      return ERR_OVERWRITE_BRANCH;

    cur->symbol = value;
    cur->is_leaf = true;
    return OK;
  }

  // -------------------------------------------------------------- //
  //  Decode — feed one bit at a time
  //
  //  bit        : 0 or 1
  //  out_symbol : set when a leaf is reached
  //  returns    : OK when a symbol was decoded,
  //               ERR_INVALID_BIT on a missing node (cursor resets),
  //               or a non-OK non-error sentinel (no symbol yet) —
  //               use the bool overload below for simpler polling
  // -------------------------------------------------------------- //
  Result decode_bit(int bit, uint16_t *out_symbol) {
    if (!cursor)
      cursor = root;

    HuffmanNode *next = bit ? cursor->right : cursor->left;

    if (!next) {
      cursor = root;
      return ERR_INVALID_BIT;
    }

    cursor = next;

    if (cursor->is_leaf) {
      *out_symbol = cursor->symbol;
      cursor = root;
      return OK;
    }

    return ERR_INVALID_BIT;
  }

  int decode_buffer(const char *buf, int bit_count, void (*on_symbol)(char)) {
    if (!buf || bit_count <= 0)
      return -1;

    int decoded = 0;
    uint16_t sym;

    for (int i = 0; i < bit_count; i++) {
      int bit = extract_bit(buf, i);
      Result r = decode_bit(bit, &sym);

      if (r == OK) {
        if (on_symbol)
          on_symbol(sym);
        decoded++;
      }
    }

    return decoded;
  }

  void reset_cursor() { cursor = root; }

  static const char *result_to_str(HuffmanTree::Result r) {
    switch (r) {
    case HuffmanTree::OK:
      return "OK";
    case HuffmanTree::ERR_NULL_BUFFER:
      return "ERR_NULL_BUFFER";
    case HuffmanTree::ERR_ZERO_BITS:
      return "ERR_ZERO_BITS";
    case HuffmanTree::ERR_PREFIX_CONFLICT:
      return "ERR_PREFIX_CONFLICT";
    case HuffmanTree::ERR_OVERWRITE_BRANCH:
      return "ERR_OVERWRITE_BRANCH";
    case HuffmanTree::ERR_INVALID_BIT:
      return "ERR_INVALID_BIT";
    default:
      return "ERR_UNKNOWN";
    }
  }

private:
  HuffmanNode *root;
  HuffmanNode *cursor;

  // Extract bit i from a packed char buffer, MSB first
  static int extract_bit(const char *buf, int i) {
    int byte_idx = i / 8;
    int bit_idx = 7 - (i % 8);
    return (buf[byte_idx] >> bit_idx) & 1;
  }

  void destroy(HuffmanNode *node) {
    if (!node)
      return;
    destroy(node->left);
    destroy(node->right);
    delete node;
  }
};

void generateCodes(uint32_t *bl_count, uint32_t *next_code, uint32_t maxBits) {
  uint32_t code = 0;
  for (uint32_t bits = 1; bits <= maxBits; bits++) {
    code = (code + bl_count[bits - 1]) << 1;
    next_code[bits] = code;
  }
}

void sort_symbols(const uint8_t *lengths, uint32_t count,
                  uint16_t *sorted_idx) {
  // 1. Initialize indices with 0, 1, 2, 3...
  for (uint32_t i = 0; i < count; i++) {
    sorted_idx[i] = (uint16_t)i;
  }

  // 2. Selection Sort: Sort indices based on the value in lengths[index]
  for (uint32_t i = 0; i < count - 1; i++) {
    uint32_t min_idx = i;
    for (uint32_t j = i + 1; j < count; j++) {

      uint8_t lenA = lengths[sorted_idx[j]];
      uint8_t lenB = lengths[sorted_idx[min_idx]];

      // Primary sort: value (ascending)
      // Secondary sort: Symbol ID (the index itself)
      uint32_t valA = lenA;
      uint32_t valB = lenB;

      if (valA < valB) {
        min_idx = j;
      } else if (valA == valB) {
        // If lengths are tied, smaller Symbol ID comes first
        if (sorted_idx[j] < sorted_idx[min_idx]) {
          min_idx = j;
        }
      }
    }

    // Swap indices
    uint16_t temp = sorted_idx[i];
    sorted_idx[i] = sorted_idx[min_idx];
    sorted_idx[min_idx] = temp;
  }
}

uint32_t distance_code_to_distance(uint32_t distance_code, const uint8_t *data,
                                   uint32_t &bitPtr) {
  static const uint32_t distance_base[30] = {
      1,    2,    3,    4,    5,    7,    9,    13,    17,    25,
      33,   49,   65,   97,   129,  193,  257,  385,   513,   769,
      1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
  static const uint8_t distance_extra_bits[30] = {
      0, 0, 0, 0, 1, 1, 2, 2,  3,  3,  4,  4,  5,  5,  6,
      6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};

  if (distance_code >= 30) {
    return 0;
  }

  return distance_base[distance_code] +
         read_bits(data, bitPtr, distance_extra_bits[distance_code]);
}

uint32_t length_code_to_length(uint32_t length_code, const uint8_t *data,
                               uint32_t &bitPtr) {
  static const uint16_t length_base[29] = {
      3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
      31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
  static const uint8_t length_extra_bits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                                1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                                4, 4, 4, 4, 5, 5, 5, 5, 0};

  if (length_code < 257 || length_code > 285) {
    return 0;
  }

  uint32_t idx = length_code - 257;
  return length_base[idx] + read_bits(data, bitPtr, length_extra_bits[idx]);
}

constexpr uint32_t staticHuffmanCodes[288] = {
    // 0-143: 8 bits, starting at 0b00110000 (48)
    48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64, 65, 66,
    67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85,
    86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103,
    104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116, 117, 118,
    119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133,
    134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148,
    149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163,
    164, 165, 166, 167, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178,
    179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191,
    // 144-255: 9 bits, starting at 0b110010000 (400)
    400, 401, 402, 403, 404, 405, 406, 407, 408, 409, 410, 411, 412, 413, 414,
    415, 416, 417, 418, 419, 420, 421, 422, 423, 424, 425, 426, 427, 428, 429,
    430, 431, 432, 433, 434, 435, 436, 437, 438, 439, 440, 441, 442, 443, 444,
    445, 446, 447, 448, 449, 450, 451, 452, 453, 454, 455, 456, 457, 458, 459,
    460, 461, 462, 463, 464, 465, 466, 467, 468, 469, 470, 471, 472, 473, 474,
    475, 476, 477, 478, 479, 480, 481, 482, 483, 484, 485, 486, 487, 488, 489,
    490, 491, 492, 493, 494, 495, 496, 497, 498, 499, 500, 501, 502, 503, 504,
    505, 506, 507, 508, 509, 510, 511,
    // 256-279: 7 bits, starting at 0b0000000 (0)
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20,
    21, 22, 23,
    // 280-287: 8 bits, starting at 0b11000000 (192)
    192, 193, 194, 195, 196, 197, 198, 199};

constexpr uint8_t staticHuffmanLengths[288] = {
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 0-15
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 16-31
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 32-47
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 48-63
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 64-79
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 80-95
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 96-111
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 112-127
    8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, // 128-143
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 144-159
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 160-175
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 176-191
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 192-207
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 208-223
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 224-239
    9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, // 240-255
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, // 256-271
    7, 7, 7, 7, 7, 7, 7, 7,                         // 272-279
    8, 8, 8, 8, 8, 8, 8, 8                          // 280-287
};

constexpr uint32_t staticDistanceCodes[30] = {
    0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14,
    15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29};

constexpr uint8_t staticDistanceLengths[30] = {5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                                               5, 5, 5, 5, 5, 5, 5, 5, 5, 5,
                                               5, 5, 5, 5, 5, 5, 5, 5, 5, 5};

Result uncompress(unsigned char *data, uint32_t length, uint8_t *buffer,
                  uint32_t uncompressed_length) {
  uint32_t bitPtr = 0;
  uint32_t CMF = read_bits(data, bitPtr, 8);
  uint32_t FLG = read_bits(data, bitPtr, 8);
  if ((CMF * 256 + FLG) % 31 != 0) {
    printf("INVALID HUFFMAN STREAM\n");
    return ERROR_CORRUPTED_DATA;
  }
  uint8_t *decompressed_data = buffer;
  uint8_t *data_iterator = decompressed_data;
  memset(decompressed_data, 0, uncompressed_length);

  while (true) {
    uint32_t bFinal = read_bits(data, bitPtr, 1);
    uint8_t compressionType = read_bits(data, bitPtr, 2);
    if (compressionType == 3) {
      return ERROR_CORRUPTED_DATA;
    }
    if (compressionType == 0) {
      uint32_t bitsToSkip = (8 - (bitPtr % 8)) % 8;
      read_bits(data, bitPtr, bitsToSkip);
      uint16_t LEN = read_bits(data, bitPtr, 16);
      uint16_t NLEN = read_bits(data, bitPtr, 16);
      if (LEN + NLEN != 0xFFFF) {
        return ERROR_CORRUPTED_DATA;
      }
      memcpy(data_iterator, data + (bitPtr >> 3), LEN);
      data_iterator += LEN;
      bitPtr += LEN * 8;

      if (bFinal)
        break;
    } else if (compressionType == 1) {
      HuffmanTree literalTree{};
      for (uint32_t i = 0; i < 288; i++) {
        literalTree.create_branch_from_code(staticHuffmanCodes[i],
                                            staticHuffmanLengths[i], i);
      }
      HuffmanTree distanceTree{};
      for (uint32_t i = 0; i < 30; i++) {
        distanceTree.create_branch_from_code(staticDistanceCodes[i],
                                             staticDistanceLengths[i], i);
      }
      uint8_t *iterator = data_iterator;
      while (true) {
        uint32_t bit = read_bits(data, bitPtr, 1);
        uint16_t out_symbol = 0;
        while (literalTree.decode_bit(bit, &out_symbol) != HuffmanTree::OK) {
          bit = read_bits(data, bitPtr, 1);
        }
        if (out_symbol == 256) {
          break;
        } else if (out_symbol < 256) {
          *iterator = out_symbol;
          iterator++;
        } else {
          if (length > 285) {
            return ERROR_CORRUPTED_DATA;
          }
          uint32_t length = length_code_to_length(out_symbol, data, bitPtr);
          uint32_t bit = read_bits(data, bitPtr, 1);
          uint16_t distance = 0;
          while (distanceTree.decode_bit(bit, &distance) != HuffmanTree::OK) {
            bit = read_bits(data, bitPtr, 1);
          }
          distance = distance_code_to_distance(distance, data, bitPtr);
          for (uint32_t j = 0; j < length; j++) {
            iterator[j] = *(iterator - distance + j);
          }
          iterator += length;
        }
        data_iterator = iterator;
      }
      if (bFinal)
        break;
    } else if (compressionType == 2) {
      uint32_t bl_count[16] = {};
      uint32_t next_codes[16] = {};

      uint32_t hlit = read_bits(data, bitPtr, 5);  // Should be around 9-20
      uint32_t hdist = read_bits(data, bitPtr, 5); // Should be around 0-31
      uint32_t hclen = read_bits(data, bitPtr, 4); // Should be around 4-15
      uint32_t codeLengthCount = hclen + 4;

      uint8_t length_symbol_values[] = {16, 17, 18, 0, 8,  7, 9,  6, 10, 5,
                                        11, 4,  12, 3, 13, 2, 14, 1, 15};

      uint16_t sorted_length_symbol_value_indices[19] = {};

      sort_symbols(length_symbol_values, codeLengthCount,
                   sorted_length_symbol_value_indices);

      uint8_t length_symbol_code_lengths[19] = {};

      HuffmanTree grand_length_code_tree{};

      for (uint32_t i = 0; i < codeLengthCount; i++) {
        uint8_t length = read_bits(data, bitPtr, 3);
        length_symbol_code_lengths[i] = length;
        bl_count[length]++;
      }

      bl_count[0] = 0;

      generateCodes(bl_count, next_codes, 15);

      for (uint32_t i = 0; i < hclen + 4; i++) {
        uint32_t value =
            length_symbol_values[sorted_length_symbol_value_indices[i]];
        uint32_t codeLength =
            length_symbol_code_lengths[sorted_length_symbol_value_indices[i]];
        uint32_t huffmanCode = next_codes[codeLength]++;

        if (codeLength != 0) {
          HuffmanTree::Result result =
              grand_length_code_tree.create_branch_from_code(huffmanCode,
                                                             codeLength, value);
          if (result != HuffmanTree::OK) {
            return ERROR_CORRUPTED_DATA;
          }
        }
      }

      // From Here On, Grand Tree Has Been Correctly Constructed

      uint8_t codeLengths[288] = {};

      memset(bl_count, 0, 16 * 4);
      memset(next_codes, 0, 16 * 4);

      uint16_t sym = 0;

      while (sym < hlit + 257) {
        uint32_t bit = read_bits(data, bitPtr, 1);
        uint16_t symbol = 0;
        while (grand_length_code_tree.decode_bit(bit, &symbol) !=
               HuffmanTree::OK) {
          bit = read_bits(data, bitPtr, 1);
        }

        uint32_t length = 0;
        uint32_t copyLength = 0;
        if (symbol == 16) {
          copyLength = read_bits(data, bitPtr, 2) + 3;
          length = codeLengths[sym - 1];
        } else if (symbol == 17) {
          copyLength = read_bits(data, bitPtr, 3) + 3;
          length = 0;
        } else if (symbol == 18) {
          copyLength = read_bits(data, bitPtr, 7) + 11;
          length = 0;
        } else {
          length = symbol;
          copyLength = 1;
        }
        memset(&codeLengths[sym], length, copyLength);
        sym += copyLength;
        bl_count[length] += copyLength;
      }

      bl_count[0] = 0;

      generateCodes(bl_count, next_codes, 15);

      HuffmanTree literalTree{};

      for (uint32_t i = 0; i < hlit + 257; i++) {
        uint32_t huffmanCodeLength = codeLengths[i];
        uint32_t huffmanCode = next_codes[huffmanCodeLength]++;

        if (huffmanCodeLength != 0) {
          HuffmanTree::Result result = literalTree.create_branch_from_code(
              huffmanCode, huffmanCodeLength, i);
          if (result != HuffmanTree::OK) {
            return ERROR_CORRUPTED_DATA;
          }
        }
      }

      // From Here Now, the Literal/Length Tree Constructed

      memset(bl_count, 0, 16 * 4);
      memset(next_codes, 0, 16 * 4);

      uint16_t distanceCodeCount = 0;

      uint8_t distanceCodeLengths[30] = {};

      while (distanceCodeCount < hdist + 1) {
        uint16_t distanceSymbol = 0;
        bool bit = read_bits(data, bitPtr, 1);
        uint16_t codeLength = 0;
        uint32_t copyLength = 0;
        while (grand_length_code_tree.decode_bit(bit, &distanceSymbol) !=
               HuffmanTree::OK) {
          bit = read_bits(data, bitPtr, 1);
        }
        if (distanceSymbol == 16) {
          codeLength = distanceCodeLengths[distanceCodeCount - 1];
          copyLength = read_bits(data, bitPtr, 2) + 3;
        } else if (distanceSymbol == 17) {
          codeLength = 0;
          copyLength = read_bits(data, bitPtr, 3) + 3;
        } else if (distanceSymbol == 18) {
          codeLength = 0;
          copyLength = read_bits(data, bitPtr, 7) + 11;
        } else {
          codeLength = distanceSymbol;
          copyLength = 1;
        }
        memset(&distanceCodeLengths[distanceCodeCount], codeLength, copyLength);
        distanceCodeCount += copyLength;
        bl_count[codeLength] += copyLength;
      }

      bl_count[0] = 0;

      generateCodes(bl_count, next_codes, 15);

      HuffmanTree distanceTree{};

      for (uint32_t i = 0; i < hdist + 1; i++) {
        uint32_t codeLength = distanceCodeLengths[i];
        uint32_t huffmanCode = next_codes[codeLength]++;

        if (codeLength != 0) {
          HuffmanTree::Result result =
              distanceTree.create_branch_from_code(huffmanCode, codeLength, i);
          if (result != HuffmanTree::OK) {
            return ERROR_CORRUPTED_DATA;
          }
        }
      }

      // Grand, Literal, Distance Trees are constructed

      uint8_t *iterator = data_iterator;
      while (true) {
        if (bitPtr > length * 8) {
          return ERROR_OUT_OF_MEMORY;
        }
        uint16_t symbol_result = 0;
        uint32_t bit = read_bits(data, bitPtr, 1);
        while (literalTree.decode_bit(bit, &symbol_result) != HuffmanTree::OK) {
          bit = read_bits(data, bitPtr, 1);
        }
        if (symbol_result == 256) {
          break;
        }
        if (symbol_result < 256) {
          *iterator = symbol_result;
          iterator++;
        }
        if (symbol_result > 256) {
          if (symbol_result > 285) {
            return ERROR_CORRUPTED_DATA;
          }
          uint32_t length = length_code_to_length(symbol_result, data, bitPtr);
          uint16_t distance = 0;

          bit = read_bits(data, bitPtr, 1);
          while (distanceTree.decode_bit(bit, &distance) != HuffmanTree::OK) {
            bit = read_bits(data, bitPtr, 1);
          }
          distance = distance_code_to_distance(distance, data, bitPtr);

          for (uint32_t k = 0; k < length; k++) {
            iterator[k] = *(iterator - distance + k);
          }
          iterator += length;
        }
      }

      data_iterator = iterator;
      if (bFinal) {
        break;
      }
    }
  }
  return SUCCESS;
}

} // namespace deflate
