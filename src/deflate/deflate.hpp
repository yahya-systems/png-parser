#ifndef DEFLATE_ALGORITHM
#define DEFLATE_ALGORITHM
#include <cstdint>

namespace deflate {

enum DResult {
  SUCCESS,
  ERROR_UNSUPPORTED_FORMAT,
  ERROR_CORRUPTED_DATA,
  ERROR_OUT_OF_MEMORY,

};

DResult uncompress(unsigned char *data, uint32_t length, uint8_t *buffer,
                   uint32_t decompressed_data_size);

} // namespace deflate

#endif
