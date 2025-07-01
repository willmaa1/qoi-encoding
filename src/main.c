#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#include "../libs/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../libs/stb_image_write.h"


struct qoi_header {
  char magic[4]; // "qoif"
  uint32_t width; // Pixels. Big endian
  uint32_t height; // Pixels. Big endian
  uint8_t channels; // 3 = rgb, 4 = rgba (just info, does not affect parsing)
  uint8_t colorspace; // 0 = srgb and linear alpha, 1 = all channels linear (just info, does not affect parsing)
};

const uint8_t headerSize = 4+4+4+1+1; // Cannot use sizeof() due to struct padding.

struct rgba {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

// 64-bit end chunk (format specified, not needed for decoding)
const uint64_t QOI_END_CHUNK = 0x0000000000000001;

// 8-bit tags
const uint8_t QOI_OP_RGB = 0b11111110; // this, R byte, G byte, B byte
const uint8_t QOI_OP_RGBA = 0b11111111; // this, R byte, G byte, B byte, A byte

// 2-bit tags
const uint8_t QOI_OP_INDEX = 0b00 << 6; // this, 6 bit index
const uint8_t QOI_OP_DIFF = 0b01 << 6; // this, 2 bit R, 2 bit G, 2 bit B (-2..1) with bias 2 (0b00 = -2) with wraparound (255 + 1 = 0)
const uint8_t QOI_OP_LUMA = 0b10 << 6; // this, 6 bit G, 4 bit R-G, 4 bit B-G. G (-32..31) with bias 32, R-G and B-G (-8..7) with bias 8. Wraparound
const uint8_t QOI_OP_RUN = 0b11 << 6; // this, 6 bit run length (1..62) with bias -1 (0b00 = 1). 63 and 64 are forbidden due to clash with 8-bit tags.

uint8_t getIndex(struct rgba rbgaStruct) {
  return (rbgaStruct.r * 3 + rbgaStruct.g * 5 + rbgaStruct.b * 7 + rbgaStruct.a * 11) % 64;
}

void decode(const char* infile, const char* outfile) {
  FILE* file = fopen(infile, "r");
  if (file == NULL) {
    printf("FILE NOT FOUND\n");
    return;
  }

  struct qoi_header qoiHeader;
  if (fread(&qoiHeader, headerSize, 1, file) != 1) {
    printf("Could not read fileheader\n");
    return;
  }
  if (memcmp(qoiHeader.magic, "qoif", 4) != 0) {
    printf("File is not a qoi file\n");
    return;
  }
  // NOTE! width and height are in big endian. We swap them now for easy usage.
  qoiHeader.width = __builtin_bswap32(qoiHeader.width);
  qoiHeader.height = __builtin_bswap32(qoiHeader.height);

  printf("QOI w:%u h:%u channels:%u color:%u\n", qoiHeader.width, qoiHeader.height, qoiHeader.channels, qoiHeader.colorspace);

  size_t totalValues = qoiHeader.height * qoiHeader.width * 4;
  printf("Reserving %u bytes for the image.\n", totalValues);
  uint8_t* imageData = malloc(totalValues);
  if (imageData == NULL) {
    printf("Not enough memory for the image!\n");
    return;
  }

  struct rgba runningArray[64];
  struct rgba prev = {0, 0, 0, 255};
  uint8_t tagByte;
  size_t pixelIndex = 0;
  while (pixelIndex < totalValues && fread(&tagByte, 1, 1, file) == 1) {
    struct rgba curr = prev;
    if (tagByte == QOI_OP_RGB) {
      size_t read = 0;
      read += fread(&curr.r, 1, 1, file);
      read += fread(&curr.g, 1, 1, file);
      read += fread(&curr.b, 1, 1, file);
      if (read != 3) {
        printf("QOI_OP_RGB read failed...\n");
        break;
      }
    } else if (tagByte == QOI_OP_RGBA) {
      size_t read = 0;
      read += fread(&curr.r, 1, 1, file);
      read += fread(&curr.g, 1, 1, file);
      read += fread(&curr.b, 1, 1, file);
      read += fread(&curr.a, 1, 1, file);
      if (read != 4) {
        printf("QOI_OP_RGBA read failed...\n");
        break;
      }
    } else {
      uint8_t tag2 = tagByte & 0b11000000;
      int8_t tagRest = tagByte & 0b00111111;
      if (tag2 == QOI_OP_INDEX) {
        curr = runningArray[tagRest];
      } else if (tag2 == QOI_OP_DIFF) {
        curr.r += ((tagRest & 0b00110000) >> 4) - 2;
        curr.g += ((tagRest & 0b00001100) >> 2) - 2;
        curr.b += ((tagRest & 0b00000011) >> 0) - 2;
      } else if (tag2 == QOI_OP_LUMA) {
        int8_t diffGreen = tagRest - 32;
        uint8_t diffOther;
        size_t read = fread(&diffOther, 1, 1, file);
        if (read != 1) {
          printf("QOI_OP_LUMA read failed...\n");
          break;
        }
        curr.g += diffGreen;
        int8_t drdg = ((diffOther & 0xF0) >> 4) - 8;
        int8_t dbdg = (diffOther & 0x0F) - 8;
        curr.r += drdg + diffGreen;
        curr.b += dbdg + diffGreen;

      } else if (tag2 == QOI_OP_RUN) {
        for (uint8_t i = 0; i < tagRest; i++) {
          imageData[pixelIndex++] = curr.r;
          imageData[pixelIndex++] = curr.g;
          imageData[pixelIndex++] = curr.b;
          imageData[pixelIndex++] = curr.a;
        }
      } else {
        printf("Error with decoding...\n");
        break;
      }
    }

    runningArray[getIndex(curr)] = curr;
    prev = curr;
    imageData[pixelIndex++] = curr.r;
    imageData[pixelIndex++] = curr.g;
    imageData[pixelIndex++] = curr.b;
    imageData[pixelIndex++] = curr.a;
  }

  // Sanity check end chunk (unnecessary for decoding)
  uint64_t qoiEnd;
  if (fread(&qoiEnd, sizeof(uint64_t), 1, file) != 1) {
    printf("Decoded qoi has missing or partially missing end chunk!\n");
  }
  qoiEnd = __builtin_bswap64(qoiEnd);
  if (qoiEnd != QOI_END_CHUNK) {
    printf("Decoded qoi has incorrect end chunk %lX\n", qoiEnd);
  }

  if (pixelIndex != totalValues) {
    printf("Missing data, partially decoded!\n");
  }
  stbi_write_png(outfile, qoiHeader.width, qoiHeader.height, 4, imageData, qoiHeader.width * 4);

  free(imageData);
}

int main(int argc, char** argv) {
  printf("\n");
  // decode("./original_qoi/dice.qoi", "file.png");
  decode("./original_qoi/edgecase.qoi", "file.png");
  // decode("./original_qoi/kodim10.qoi", "file.png");
  // decode("./original_qoi/kodim23.qoi", "file.png");
  // decode("./original_qoi/qoi_logo.qoi", "file.png");
  // decode("./original_qoi/testcard_rgba.qoi", "file.png");
  // decode("./original_qoi/testcard.qoi", "file.png");
  // decode("./original_qoi/wikipedia_008.qoi", "file.png");
  printf("\n");

  return 0;
}

