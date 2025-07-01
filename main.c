#include <stdio.h>
#include <stdint.h>
#include <string.h>

struct qoi_header {
  char magic[4]; // "qoif"
  uint32_t width; // Pixels. Big endian
  uint32_t height; // Pixels. Big endian
  uint8_t channels; // 3 = rgb, 4 = rgba (just info, does not affect parsing)
  uint8_t colorspace; // 0 = srgb and linear alpha, 1 = all channels linear (just info, does not affect parsing)
};

struct rgba {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
};

// 8-bit tags
const uint8_t QOI_OP_RBG = 0b11111110; // this, R byte, G byte, B byte
const uint8_t QOI_OP_RBGA = 0b11111111; // this, R byte, G byte, B byte, A byte

// 2-bit tags
const uint8_t QOI_OP_INDEX = 0b00 << 6; // this, 6 bit index
const uint8_t QOI_OP_DIFF = 0b01 << 6; // this, 2 bit R, 2 bit G, 2 bit B (-2..1) with bias 2 (0b00 = -2) with wraparound (255 + 1 = 0)
const uint8_t QOI_OP_LUMA = 0b10 << 6; // this, 6 bit G, 4 bit R-G, 4 bit B-G. G (-32..31) with bias 32, R-G and B-G (-8..7) with bias 8. Wraparound
const uint8_t QOI_OP_RUN = 0b11 << 6; // this, 6 bit run length (1..62) with bias -1 (0b00 = 1). 63 and 64 are forbidden due to clash with 8-bit tags.


uint8_t getIndex(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
  return (r * 3 + g * 5 + b * 7 + a * 11) % 64;
}

void decode(const char* filename) {
  struct rgba runningArray[64];
  struct rgba prev = {0, 0, 0, 255};
  FILE* file = fopen(filename, "r");
  if (file == NULL) {
    printf("FILE NOT FOUND\n");
    return;
  }

  struct qoi_header qoiHeader;
  size_t read = fread(&qoiHeader, sizeof(struct qoi_header), 1, file);
  if (read != 1) {
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

  
}

int main(int argc, char** argv) {
  printf("\n");
  decode("./original_qoi/kodim23.qoi");
  // decode("./original_png/kodim23.png");
  printf("\n");
}

