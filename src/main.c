#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

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
  FILE* file = fopen(infile, "rb");
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

  // printf("QOI w:%u h:%u channels:%u color:%u\n", qoiHeader.width, qoiHeader.height, qoiHeader.channels, qoiHeader.colorspace);

  size_t totalValues = qoiHeader.height * qoiHeader.width * 4;
  // printf("Reserving %lu bytes for the image.\n", totalValues);
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
      runningArray[getIndex(curr)] = curr;
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
      runningArray[getIndex(curr)] = curr;
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
        runningArray[getIndex(curr)] = curr;
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

        runningArray[getIndex(curr)] = curr;
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

void encode(const char* infile, const char* outfile) {
  int width;
  int height;
  int channels;
  if(!stbi_info(infile, &width, &height, &channels)) {
    printf("Cannot read image info from infile %s.\n", infile);
    return;
  }

  if(channels != 3) {
    channels = 4;
  }

  uint8_t* pixels = (uint8_t *)stbi_load(infile, &width, &height, NULL, channels);

  if (pixels == NULL) {
    printf("Couldn't load image file.\n");
    return;
  }

  FILE* file = fopen(outfile, "wb");
  if (file == NULL) {
    printf("FILE NOT FOUND\n");
    return;
  }

  // TODO: now all images are marked as sRGB. Enable linear rgb
  const uint8_t colorspace = 1;
  struct qoi_header qoiHeader = {"qoif", width, height, channels, colorspace};

  size_t totalValues = ((size_t)qoiHeader.width) * qoiHeader.height * qoiHeader.channels;
  // printf("Width %u height %u channels %u (%lu).\n", qoiHeader.width, qoiHeader.height, channels, totalValues);

  uint32_t widthBE = __builtin_bswap32(qoiHeader.width);
  uint32_t heightBE = __builtin_bswap32(qoiHeader.height);
  fwrite(&qoiHeader.magic, 4, 1, file);
  fwrite(&widthBE, 4, 1, file);
  fwrite(&heightBE, 4, 1, file);
  fwrite(&channels, 1, 1, file);
  fwrite(&qoiHeader.colorspace, 1, 1, file);

  const int hasAlpha = channels == 4;
  uint8_t runlength = 0;
  size_t pixelIndex = 0;
  struct rgba prev = {0, 0, 0, 255};
  struct rgba runningArray[64]; // Zero-initialized
  while (pixelIndex < totalValues) {
    uint8_t r = *(pixels + pixelIndex++);
    uint8_t g = *(pixels + pixelIndex++);
    uint8_t b = *(pixels + pixelIndex++);
    uint8_t a = hasAlpha ? *(pixels + pixelIndex++) : 255;
    struct rgba curr = {r, g, b, a};
    if (prev.r == curr.r && prev.g == curr.g && prev.b == curr.b && prev.a == curr.a) {
      // RUN using previous pixel
      ++runlength;
      // Max 62 runlength. 63 and 64 are reserved.
      // Note that we use bias -1 so check against 62.
      if (runlength == 62) {
        runlength--;
        runlength |= QOI_OP_RUN;
        fwrite(&runlength, 1, 1, file);
        // printf("Max run %02X\n", runlength);
        runlength = 0;
      }

      // Edgecase, using default prev pixel at the start requires runningArray to be updated.
      // This is due to alpha of default pixel being 255, not 0.
      if (pixelIndex == (hasAlpha ? 4 : 3)) {
        runningArray[getIndex(curr)] = curr;
      }
      continue;
    }

    // Save RUN that was stopped before max
    if (runlength > 0) {
      // Note that we use bias -1
      runlength--;
      runlength |= QOI_OP_RUN;
      fwrite(&runlength, 1, 1, file);
      runlength = 0;
    }

    // INDEX
    uint8_t possibleIndex = getIndex(curr);
    struct rgba possibleMatch = runningArray[possibleIndex];
    if (possibleMatch.r == curr.r &&
        possibleMatch.g == curr.g &&
        possibleMatch.b == curr.b &&
        possibleMatch.a == curr.a) {

      possibleIndex |= QOI_OP_INDEX;
      fwrite(&possibleIndex, 1, 1, file);
      prev = curr;
      continue;
    }

    // Update pixel to runningArray
    runningArray[getIndex(curr)] = curr;

    if (hasAlpha && curr.a != prev.a) {
      // Only way to change alpha (besides index) is RGBA
      fwrite(&QOI_OP_RGBA, 1, 1, file);
      fwrite(&curr.r, 1, 1, file);
      fwrite(&curr.g, 1, 1, file);
      fwrite(&curr.b, 1, 1, file);
      fwrite(&curr.a, 1, 1, file);
      prev = curr;
      continue;
    }
    
    // Take advantage or wrapping and bias for easy comparisons
    uint8_t diffr2 = curr.r - prev.r + 2;
    uint8_t diffg2 = curr.g - prev.g + 2;
    uint8_t diffb2 = curr.b - prev.b + 2;
    if (diffr2 <= 3 && diffg2 <= 3 && diffb2 <= 3) {
      uint8_t fullbyte = QOI_OP_DIFF | (diffr2 << 4) | (diffg2 << 2) | diffb2;
      fwrite(&fullbyte, 1, 1, file);
      prev = curr;
      continue;
    }

    // Take advantage or wrapping and bias for easy comparisons
    uint8_t diffg3 = curr.g - prev.g;
    uint8_t diffgg = curr.g - prev.g + 32;
    uint8_t diffrg = curr.r - prev.r - diffg3 + 8;
    uint8_t diffbg = curr.b - prev.b - diffg3 + 8;
    if (diffgg <= 63 && diffrg <= 15 && diffbg <= 15) {
      uint8_t fullbyte1 = QOI_OP_LUMA | diffgg;
      uint8_t fullbyte2 = (diffrg << 4) | diffbg;
      fwrite(&fullbyte1, 1, 1, file);
      fwrite(&fullbyte2, 1, 1, file);
      prev = curr;
      continue;
    }

    // Use RGB if nothing else works
    fwrite(&QOI_OP_RGB, 1, 1, file);
    fwrite(&curr.r, 1, 1, file);
    fwrite(&curr.g, 1, 1, file);
    fwrite(&curr.b, 1, 1, file);
    prev = curr;
  }

  // Save RUN if it was still ongoing
  if (runlength > 0) {
    // Note that we use bias -1
    runlength--;
    runlength |= QOI_OP_RUN;
    fwrite(&runlength, 1, 1, file);
    runlength = 0;
  }

  // printf("Pixels %lu total %lu\n", pixelIndex, totalValues);

  uint64_t endChunkBE = __builtin_bswap64(QOI_END_CHUNK);
  fwrite(&endChunkBE, 8, 1, file);

  fclose(file);

  free(pixels);
}

int main(int argc, char** argv) {
  printf("\n");
  // decode("./original_qoi/dice.qoi", "file.png");
  // decode("./original_qoi/edgecase.qoi", "file.png");
  // decode("./original_qoi/kodim10.qoi", "file.png");
  // decode("./original_qoi/kodim23.qoi", "file.png");
  // decode("./original_qoi/qoi_logo.qoi", "file.png");
  // decode("./original_qoi/testcard_rgba.qoi", "file.png");
  // decode("./original_qoi/testcard.qoi", "file.png");
  // decode("./original_qoi/wikipedia_008.qoi", "file.png");

  // encode("./original_png/dice.png", "file.qoi");
  // encode("./original_png/edgecase.png", "file.qoi");
  // encode("./original_png/kodim10.png", "file.qoi");
  // encode("./original_png/kodim23.png", "file.qoi");
  // encode("./original_png/qoi_logo.png", "file.qoi");
  // encode("./original_png/testcard_rgba.png", "file.qoi");
  // encode("./original_png/testcard.png", "file.qoi");
  // encode("./original_png/wikipedia_008.png", "file.qoi");

  // decode("./file.qoi", "file.png");

  // Test encoding and decoding all the images
  clock_t begin = clock();

  encode("./original_png/dice.png", "./encoded/dice.qoi");
  encode("./original_png/edgecase.png", "./encoded/edgecase.qoi");
  encode("./original_png/kodim10.png", "./encoded/kodim10.qoi");
  encode("./original_png/kodim23.png", "./encoded/kodim23.qoi");
  encode("./original_png/qoi_logo.png", "./encoded/qoi_logo.qoi");
  encode("./original_png/testcard_rgba.png", "./encoded/testcard_rgba.qoi");
  encode("./original_png/testcard.png", "./encoded/testcard.qoi");
  encode("./original_png/wikipedia_008.png", "./encoded/wikipedia_008.qoi");

  clock_t mid = clock();

  decode("./encoded/dice.qoi", "./decoded/dice.png");
  decode("./encoded/edgecase.qoi", "./decoded/edgecase.png");
  decode("./encoded/kodim10.qoi", "./decoded/kodim10.png");
  decode("./encoded/kodim23.qoi", "./decoded/kodim23.png");
  decode("./encoded/qoi_logo.qoi", "./decoded/qoi_logo.png");
  decode("./encoded/testcard_rgba.qoi", "./decoded/testcard_rgba.png");
  decode("./encoded/testcard.qoi", "./decoded/testcard.png");
  decode("./encoded/wikipedia_008.qoi", "./decoded/wikipedia_008.png");

  clock_t end = clock();
  double time_spent_mid = (double)(mid - begin) / CLOCKS_PER_SEC;
  double time_spent_end = (double)(end - mid) / CLOCKS_PER_SEC;
  double time_spent_tot = time_spent_mid + time_spent_end;

  // Encoding: >0.27s
  // Decoding: >1.64s
  // Total: >1.9s
  printf("Encoding: %f sec.\n", time_spent_mid);
  printf("Decoding: %f sec.\n", time_spent_end);
  printf("Totaltim: %f sec.\n", time_spent_tot);
  printf("\n");

  return 0;
}

