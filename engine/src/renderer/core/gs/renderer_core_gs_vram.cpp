/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <dma.h>
#include <graph.h>
#include <gs_psm.h>
#include "renderer/core/gs/renderer_core_gs_vram.hpp"

#define GS_VRAM_TEXTURE_ALIGNMENT GRAPH_ALIGN_BLOCK
#define GS_VRAM_BUFFER_ALIGNMENT GRAPH_ALIGN_PAGE

namespace Tyra {

RendererCoreGSVRam::RendererCoreGSVRam() {
  touched = false;
  pointer = 0;
  cachedFreeSpace = getFreeSpaceInMB();
}

RendererCoreGSVRam::~RendererCoreGSVRam() {}

const float& RendererCoreGSVRam::getFreeSpaceInMB() {
  if (touched) {
    cachedFreeSpace = (GRAPH_VRAM_MAX_WORDS - pointer) / ptr2MB;
    touched = false;
  }

  return cachedFreeSpace;
}

float RendererCoreGSVRam::getSizeInMB(const Texture& texture) {
  float result = getSizeInMB(*texture.core);

  if (texture.clut->data != nullptr) {
    result += getSizeInMB(*texture.clut);
  }

  return result;
}

float RendererCoreGSVRam::getSizeInMB(const TextureData& texData) {
  return getSizeInMB(texData.width, texData.height, texData.psm,
                     GS_VRAM_TEXTURE_ALIGNMENT);
}

float RendererCoreGSVRam::getSizeInMB(int width, int height, const int& psm,
                                      const int& alignment) {
  return getSize(width, height, psm, alignment) / ptr2MB;
}

int RendererCoreGSVRam::allocate(const TextureData& texData) {
  return allocate(texData.width, texData.height, texData.psm,
                  GS_VRAM_TEXTURE_ALIGNMENT);
}

int RendererCoreGSVRam::allocateBuffer(const int& width, const int& height,
                                       const int& psm) {
  return allocate(width, height, psm, GS_VRAM_BUFFER_ALIGNMENT);
}

int RendererCoreGSVRam::allocate(const int& width, const int& height,
                                 const int& psm, const int& alignment) {
  auto size = getSize(width, height, psm, alignment);

  // // printf("size: %d\n", size);
  pointer += size;

  // If the pointer overflows the vram size
  if (pointer > GRAPH_VRAM_MAX_WORDS) {
    pointer -= size;
    return -1;
  }

  touched = true;

  // // printf("pointer: %d\n", pointer);
  // // printf("gs address: %d\n", pointer - size);

  return pointer - size;
}

void RendererCoreGSVRam::free(const int& address) {
  pointer = address;
  touched = true;
}

int RendererCoreGSVRam::getSize(int width, int height, const int psm,
                                const int alignment) {
  if (width == 0 && height == 0) {
    return 0;
  }

  int size = 0;
  int widthPixel = 0;
  int heightPixel = 0;
  int widthBlock = 0;
  int heightBlock = 0;
  int widthPage = 0;
  int heightPage = 0;
  int totalWidthPage = 1;   // use 1 page representing the actual page working
  int totalHeightPage = 1;  // use 1 page representing the actual page working
  int carryWidth = 0;
  int carryHeight = 0;
  int block2x2 = 0;

  if (psm == GS_PSM_4) {
    /**
     * 1 Page = 32 blocks
     * Each block represents 32x16 pixels.
     *  128 pixels
     * -------------
     * |           |
     * |00|02|08|09| -|
     * |01|03|10|11|  |
     * |04|05|12|13|  |
     * |06|07|14|15|  |
     * |16|18|24|26|  | 128 pixels
     * |17|19|25|27|  |
     * |20|22|28|30|  |
     * |21|23|29|31| -|
     **/
    widthPixel = 32;
    heightPixel = 16;
    widthPage = 128;
    heightPage = 128;
  } else if (psm == GS_PSM_8) {
    /**
     * 1 Page = 32 blocks
     * Each block represents 16x16 pixels.
     *        128 pixels
     * -------------------------
     * |                       |
     * |00|01|04|05|16|17|20|21| --
     * |02|03|06|07|18|19|22|23| -| 64 pixels
     * |08|09|12|13|24|25|28|29| -|
     * |10|11|14|15|26|27|30|31| --
     **/
    widthPixel = 16;
    heightPixel = 16;
    widthPage = 128;
    heightPage = 64;
  } else if (psm == GS_PSM_24 || psm == GS_PSM_32 || psm == GS_PSM_8H ||
             psm == GS_PSM_4HL || psm == GS_PSM_4HH || psm == GS_PSMZ_24 ||
             psm == GS_PSMZ_32) {
    /**
     * 1 Page = 32 blocks
     * Each block represents 8x8 pixels.
     *        64 pixels
     * -------------------------
     * |                       |
     * |00|01|04|05|16|17|20|21| --
     * |02|03|06|07|18|19|22|23| -| 32 pixels
     * |08|09|12|13|24|25|28|29| -|
     * |10|11|14|15|26|27|30|31| --
     **/
    widthPixel = 8;
    heightPixel = 8;
    widthPage = 64;
    heightPage = 32;
  } else if (psm == GS_PSM_16 || psm == GS_PSMZ_16 || psm == GS_PSM_16S ||
             psm == GS_PSMZ_16S) {
    /**
     * 1 Page = 32 blocks
     * Each block represents 16x8 pixels.
     *  64 pixels
     * -------------
     * |           |
     * |00|02|08|09| -|
     * |01|03|10|11|  |
     * |04|05|12|13|  |
     * |06|07|14|15|  |
     * |16|18|24|26|  | 64 pixels
     * |17|19|25|27|  |
     * |20|22|28|30|  |
     * |21|23|29|31| -|
     **/
    widthPixel = 16;
    heightPixel = 8;
    widthPage = 64;
    heightPage = 64;
  }

  totalWidthPage += width / widthPage;
  totalHeightPage += height / heightPage;

  /**
   * Delete 1 page.
   * Represents the current page you are working on
   * and gets the actual block size
   * Here it reduces everything to the first 32 blocks.
   * */

  width = width - widthPage * (totalWidthPage - 1);
  height = height - heightPage * (totalHeightPage - 1);

  /**
   * If the width or height is 0.
   * It means that it completes the page and does not create another one.
   **/

  if (width == 0) {
    totalWidthPage--;
    width = widthPage;
  }

  if (height == 0) {
    totalHeightPage--;
    height = heightPage;
  }

  /**
   * Gets the total size of the pages minus the actual page working on,
   * to get the actual block size.
   **/

  size += ((totalWidthPage * totalHeightPage) - 1) * 32;

  widthBlock = width / widthPixel;
  heightBlock = height / heightPixel;

  if (psm == GS_PSM_4 || psm == GS_PSM_16) {
    /**
     * This gets the 2x2 block sections of the page that need to be included.
     * |0|0|2|2|
     * |0|0|2|2|
     * |1|1|3|3|
     * |1|1|3|3|
     * |4|4|5|5|
     * |4|4|5|5|
     * |6|6|7|7|
     * |6|6|7|7|
     * section * 4 (blocks) = n blocks
     **/

    if (widthBlock <= 2 && heightBlock <= 2) {
      size += 0;  //  0 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 4) {
      size += 4;  //  1 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 2) {
      size += 8;  //  2 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 4) {
      size += 12;  // 3 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 6) {
      size += 16;  // 4 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 8) {
      size += 20;  // 5 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 6) {
      size += 24;  // 6 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 8) {
      size += 28;  // 7 * 4 blocks
    }
  } else if (psm == GS_PSM_8 || psm == GS_PSM_24 || psm == GS_PSM_32 ||
             psm == GS_PSM_8H || psm == GS_PSM_4HL || psm == GS_PSM_4HH ||
             psm == GS_PSMZ_24 || psm == GS_PSMZ_32) {
    /**
     * This gets the 2x2 block sections of the page that need to be included.
     * |0|0|1|1|4|4|5|5|
     * |0|0|1|1|4|4|5|5|
     * |2|2|3|3|6|6|7|7|
     * |2|2|3|3|6|6|7|7|
     * section * 4 (blocks) = n blocks
     **/
    if (widthBlock <= 2 && heightBlock <= 2) {
      size += 0;  // section 0 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 2) {
      size += 4;  // section 1 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 4) {
      size += 8;  // section 2 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 4) {
      size += 12;  // 3 * 4 blocks
    } else if (widthBlock <= 6 && heightBlock <= 2) {
      size += 16;  // 4 * 4 blocks
    } else if (widthBlock <= 8 && heightBlock <= 2) {
      size += 20;  // 5 * 4 blocks
    } else if (widthBlock <= 6 && heightBlock <= 4) {
      size += 24;  // 6 * 4 blocks
    } else if (widthBlock <= 8 && heightBlock <= 4) {
      size += 28;  // 7 * 4 blocks
    }
  } else if (psm == GS_PSMZ_16) {
    /**
     * This gets the 2x2 block sections of the page that need to be included.
     * Each block represents 16x8 pixels.
     * |6|6|4|4|
     * |6|6|4|4|
     * |7|7|5|5|
     * |7|7|5|5|
     * |2|2|0|0|
     * |2|2|0|0|
     * |3|3|1|1|
     * |3|3|1|1|
     * section * 4 (blocks) = n blocks 16x8
     **/

    if (widthBlock <= 2 && heightBlock <= 2) {
      size += 6 * 4;  // 6 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 4) {
      size += 7 * 4;  // 7 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 2) {
      size += 4 * 4;  // 4 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 4) {
      size += 5 * 4;  // 5 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 6) {
      size += 2 * 4;  // 2 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 8) {
      size += 3 * 4;  // 3 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 6) {
      size += 0;  // 0 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 8) {
      size += 4;  // 1 * 4 blocks
    }
  } else if (psm == GS_PSM_16S) {
    /**
     * This gets the 2x2 block sections of the page that need to be included.
     * Each block represents 16x8 pixels.
     * |0|0|4|4|
     * |0|0|4|4|
     * |2|2|6|6|
     * |2|2|6|6|
     * |1|1|5|5|
     * |1|1|5|5|
     * |3|3|7|7|
     * |3|3|7|7|
     * section * 4 (blocks) = n blocks 16x8
     **/

    if (widthBlock <= 2 && heightBlock <= 2) {
      size += 0;  //  0 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 4) {
      size += 8;  //  2 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 2) {
      size += 16;  //  4 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 4) {
      size += 24;  // 6 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 6) {
      size += 4;  // 4 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 8) {
      size += 12;  // 3 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 6) {
      size += 4;  // 1 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 8) {
      size += 28;  // 7 * 4 blocks
    }
  } else if (psm == GS_PSMZ_16S) {
    /**
     * This gets the 2x2 block sections of the page that need to be included.
     * Each block represents 16x8 pixels.
     * |6|6|2|2|
     * |6|6|2|2|
     * |4|4|0|0|
     * |4|4|0|0|
     * |7|7|3|3|
     * |7|7|3|3|
     * |5|5|1|1|
     * |5|5|1|1|
     * section * 4 (blocks) = n blocks 16x8
     **/
    if (widthBlock <= 2 && heightBlock <= 2) {
      size += 24;  //  6 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 4) {
      size += 16;  //  4 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 2) {
      size += 8;  //  2 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 4) {
      size += 0;  // 0 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 6) {
      size += 28;  // 7 * 4 blocks
    } else if (widthBlock <= 2 && heightBlock <= 8) {
      size += 20;  // 5 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 6) {
      size += 12;  // 3 * 4 blocks
    } else if (widthBlock <= 4 && heightBlock <= 8) {
      size += 4;  // 1 * 4 blocks
    }
  }

  // This fixes sprites less than 1 block.

  if (widthBlock == 0) {
    widthBlock = 1;
  }

  if (heightBlock == 0) {
    heightBlock = 1;
  }

  carryWidth = widthBlock % 2;
  carryHeight = heightBlock % 2;

  if (psm == GS_PSM_4 || psm == GS_PSM_16 || psm == GS_PSMZ_16 ||
      psm == GS_PSM_16S || psm == GS_PSMZ_16S) {
    /**
     * This gets the position of the first 4 blocks.
     * knowing if the number of blocks is odd or not.
     * |0|2|0|2|
     * |1|3|1|2|
     * |0|2|0|2|
     * |1|3|1|2|
     * |0|2|0|2|
     * |1|3|1|2|
     * |0|2|0|2|
     * |1|3|1|2|
     * 
     * carryWidth  = bit 2
     * carryHeight = bit 1
     * ([(bit 2)(bit 1)] ^ 3) + 1  = Block size
     * N | Binary | Decimal |B XOR 3| +1 | Block size
     * 0 |   11   |    3    |   0   |  1 | 1 block
     * 1 |   10   |    2    |   1   |  2 | 2 block
     * 2 |   01   |    1    |   2   |  3 | 3 block
     * 3 |   00   |    0    |   3   |  4 | 4 block
     **/

    block2x2 = ((1 & carryWidth) << 1) | carryHeight;
  } else if (psm == GS_PSM_8 || psm == GS_PSM_24 || psm == GS_PSM_32 ||
             psm == GS_PSM_8H || psm == GS_PSM_4HL || psm == GS_PSM_4HH ||
             psm == GS_PSMZ_24 || psm == GS_PSMZ_32) {
    /**
     * This gets the position of the first 4 blocks.
     * knowing if the number of blocks is odd or not.
     * |0|1|0|1|0|1|0|1|
     * |2|3|2|3|2|3|2|3|
     * |0|1|0|1|0|1|0|1|
     * |2|3|2|3|2|3|2|3|
     * 
     * carryHeight = bit 2
     * carryWidth  = bit 1
     * ([(bit 2)(bit 1)] ^ 3) + 1  = Block size
     * N | Binary | Decimal |B XOR 3| +1 | Block size
     * 0 |   11   |    3    |   0   |  1 | 1 block
     * 1 |   10   |    2    |   1   |  2 | 2 block
     * 2 |   01   |    1    |   2   |  3 | 3 block
     * 3 |   00   |    0    |   3   |  4 | 4 block
     **/
    block2x2 = ((1 & carryHeight) << 1) | carryWidth;
  }
  block2x2 = block2x2 ^ 3;
  block2x2++;

  size += block2x2;

  size *= GRAPH_ALIGN_BLOCK;

  size = -alignment & (size + (alignment - 1));

  return size;
}

}  // namespace Tyra
