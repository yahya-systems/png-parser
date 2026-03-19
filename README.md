# PNG Parser

A PNG image parser with custom DEFLATE decoder implementation.

## Why
- I have decided to go with a custom deflate implementation for this png parser purely in order to keep it dependency free.

## Features
- Full PNG chunk parsing
- Custom DEFLATE decompression (Huffman coding, LZ77)
- Support for multiple color types (grayscale, RGB, RGBA)
- Channel conversions
- Static and non-compressed Huffman blocks
- No support for 16 bit PNGs, Adam7 Interlacing, and indexed PNGs.
- A BMP parser too.

## Usage
- using png-parser is simple, all required is to call the function :

```cpp
openImage(imageUrl, *width, *height, *channel, outputChannel, flip)
```

- `imageUrl` is basically the image file path.
- `width` & `height` are pointers the png-parser would fill the image's width and height to.
- `channel` is a pointer that png-parser fills with the original color channel of the image.
- `outputChannel` is used to to specify the desired output channel of the image, png-parser will perform the required conversions.
- `flip` tells the parser whether to flip the image vertically or no.

## Architecture
In the earlier version of this png-parser, the deflate implementation used trees, which has proven to be quite slow, taking 40 ms to decompress the data
(this is purely talking about deflate decompression not the entire image pipeline). So in the next version, i switched to using 10+5 Bit Look up Tables.
with 10 bits covering 90% of Huffman codes.

## Performance
To fully decode a 1200x1200 image, the current implementation takes ~22 ms.
The deflate pipeline itself takes ~12.5 ms

### Benchmarks are done in :
- Reference image `goku.png`
- M3 Macbook Air.
- Compiler: clang/gcc -O3

