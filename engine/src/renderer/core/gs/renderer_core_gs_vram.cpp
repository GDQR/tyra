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
  // // printf("getSizeInMB width, height: %d,%d\n", width, height);
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
  int section = 0;
  int widthPage = 0;
  int heightPage = 0;
  int totalWidthPage = 1;   // use 1 page representing the actual page working
  int totalHeightPage = 1;  // use 1 page representing the actual page working
  int totalPages = 0;
  int carryWidth = 0;
  int carryHeight = 0;

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

  printf("width: %d\n", width);
  printf("height: %d\n", height);

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
   * delete 1 page
   * representing the actual page and get the truth block size
   **/

  totalPages = (totalWidthPage * totalHeightPage) - 1;

  if (totalPages < 0) {
    totalPages = 0;
  }

  printf("page width,heigth: %d,%d\n", totalWidthPage, totalHeightPage);
  printf("page totalPages: %d\n", totalPages);

  printf("resize width: %d\n", width);
  printf("resize height: %d\n", height);
  if (psm == GS_PSM_4) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
     * Each block represents 32x16 pixels.
     * |0|0|2|2|
     * |0|0|2|2|
     * |1|1|3|3|
     * |1|1|3|3|
     * |4|4|5|5|
     * |4|4|5|5|
     * |6|6|7|7|
     * |6|6|7|7|
     * section * 4 (blocks) = n blocks 32x16
     **/

    if (width <= 64 && height <= 32) {
      section = 0;  //  0 * 4 blocks 32x16
    } else if (width <= 64 && height <= 64) {
      section = 4;  //  1 * 4 blocks 32x16
    } else if (width <= 128 && height <= 32) {
      section = 8;  //  2 * 4 blocks 32x16
    } else if (width <= 128 && height <= 64) {
      section = 12;  // 3 * 4 blocks 32x16
    } else if (width <= 64 && height <= 96) {
      section = 16;  // 4 * 4 blocks 32x16
    } else if (width <= 64 && height <= 128) {
      section = 20;  // 5 * 4 blocks 32x16
    } else if (width <= 128 && height <= 96) {
      section = 24;  // 6 * 4 blocks 32x16
    } else if (width <= 128 && height <= 128) {
      section = 28;  // 7 * 4 blocks 32x16
    }

  } else if (psm == GS_PSM_8) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
     * Each block represents 16x16 pixels.
     * |0|0|1|1|4|4|5|5|
     * |0|0|1|1|4|4|5|5|
     * |2|2|3|3|6|6|7|7|
     * |2|2|3|3|6|6|7|7|
     * section * 4 (blocks) = n blocks 16x16
     **/

    if (width <= 32 && height <= 32) {
      section = 0;  // section 0 * 4 blocks 16x16
    } else if (width <= 64 && height <= 32) {
      section = 4;  // section 1 * 4 blocks 16x16
    } else if (width <= 32 && height <= 64) {
      section = 8;  // section 2 * 4 blocks 16x16
    } else if (width <= 64 && height <= 64) {
      section = 12;  // 3 * 4 blocks 16x16
    } else if (width <= 96 && height <= 32) {
      section = 16;  // 4 * 4 blocks 16x16
    } else if (width <= 128 && height <= 32) {
      section = 20;  // 5 * 4 blocks 16x16
    } else if (width <= 96 && height <= 64) {
      section = 24;  // 6 * 4 blocks 16x16
    } else if (width <= 128 && height <= 64) {
      section = 28;  // 7 * 4 blocks 16x16
    }
  } else if (psm == GS_PSM_16) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
     * Each block represents 16x8 pixels.
     * |0|0|2|2|
     * |0|0|2|2|
     * |1|1|3|3|
     * |1|1|3|3|
     * |4|4|5|5|
     * |4|4|5|5|
     * |6|6|7|7|
     * |6|6|7|7|
     * section * 4 (blocks) = n blocks 16x8
     **/

    if (width <= 32 && height <= 16) {
      section = 0;  // section 0 * 4 blocks 16x8
    } else if (width <= 32 && height <= 32) {
      section = 4;  //  1 * 4 blocks 16x8
    } else if (width <= 64 && height <= 16) {
      section = 8;  //  2 * 4 blocks 16x8
    } else if (width <= 64 && height <= 32) {
      section = 12;  // 3 * 4 blocks 16x8
    } else if (width <= 32 && height <= 48) {
      section = 16;  // 4 * 4 blocks 16x8
    } else if (width <= 32 && height <= 64) {
      section = 20;  // 5 * 4 blocks 16x8
    } else if (width <= 64 && height <= 48) {
      section = 24;  // 6 * 4 blocks 16x8
    } else if (width <= 64 && height <= 64) {
      section = 28;  // 7 * 4 blocks 16x8
    }

  } else if (psm == GS_PSMZ_16) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
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

    if (width <= 32 && height <= 16) {
      section = 6 * 4;
    } else if (width <= 32 && height <= 32) {
      section = 7 * 4;
    } else if (width <= 64 && height <= 16) {
      section = 4 * 4;
    } else if (width <= 64 && height <= 32) {
      section = 5 * 4;
    } else if (width <= 32 && height <= 48) {
      section = 2 * 4;
    } else if (width <= 32 && height <= 64) {
      section = 3 * 4;
    } else if (width <= 64 && height <= 48) {
      section = 0;
    } else if (width <= 64 && height <= 64) {
      section = 1 * 4;
    }
  } else if (psm == GS_PSM_16S) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
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

    if (width <= 32 && height <= 16) {
      section = 0;
    } else if (width <= 32 && height <= 32) {
      section = 2 * 4;
    } else if (width <= 64 && height <= 16) {
      section = 4 * 4;
    } else if (width <= 64 && height <= 32) {
      section = 6 * 4;
    } else if (width <= 32 && height <= 48) {
      section = 1 * 4;
    } else if (width <= 32 && height <= 64) {
      section = 3 * 4;
    } else if (width <= 64 && height <= 48) {
      section = 5 * 4;
    } else if (width <= 64 && height <= 64) {
      section = 7 * 4;
    }
  } else if (psm == GS_PSMZ_16S) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
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

    if (width <= 32 && height <= 16) {
      section = 6 * 4;
    } else if (width <= 32 && height <= 32) {
      section = 4 * 4;
    } else if (width <= 64 && height <= 16) {
      section = 2 * 4;
    } else if (width <= 64 && height <= 32) {
      section = 0 * 4;
    } else if (width <= 32 && height <= 48) {
      section = 7 * 4;
    } else if (width <= 32 && height <= 64) {
      section = 5 * 4;
    } else if (width <= 64 && height <= 48) {
      section = 4 * 4;
    } else if (width <= 64 && height <= 64) {
      section = 1 * 4;
    }
  } else if (psm == GS_PSM_24 || psm == GS_PSM_32 || psm == GS_PSM_8H ||
             psm == GS_PSM_4HL || psm == GS_PSM_4HH || psm == GS_PSMZ_24 ||
             psm == GS_PSMZ_32) {
    /**
     * This gets the 4x4 block sections of the page that need to be included.
     * Each block represents 8x8 pixels.
     * |0|0|1|1|4|4|5|5|
     * |0|0|1|1|4|4|5|5|
     * |2|2|3|3|6|6|7|7|
     * |2|2|3|3|6|6|7|7|
     * section * 4 (blocks) = n blocks 8x8
     **/

    if (width <= 16 && height <= 16) {
      section = 0;  // section 0 * 4 blocks 8x8
    } else if (width <= 32 && height <= 16) {
      section = 4;  // section 1 * 4 blocks 8x8
    } else if (width <= 16 && height <= 32) {
      section = 8;  // section 2 * 4 blocks 8x8
    } else if (width <= 32 && height <= 32) {
      section = 12;  // 3 * 4 blocks 8x8
    } else if (width <= 48 && height <= 16) {
      section = 16;  // 4 * 4 blocks 8x8
    } else if (width <= 64 && height <= 16) {
      section = 20;  // 5 * 4 blocks 8x8
    } else if (width <= 48 && height <= 32) {
      section = 24;  // 6 * 4 blocks 8x8
    } else if (width <= 64 && height <= 32) {
      section = 28;  // 7 * 4 blocks 8x8
    }
  }

  printf("Section: %d\n", section);

  widthBlock = width / widthPixel;
  heightBlock = height / heightPixel;

  printf("1 widthBlock: %d\n", widthBlock);
  printf("1 heightBlock: %d\n", heightBlock);

  if (widthBlock == 0) {
    widthBlock = 1;
  }

  if (heightBlock == 0) {
    heightBlock = 1;
  }

  carryWidth = widthBlock % 2;
  carryHeight = heightBlock % 2;

  if (carryWidth == 0) {
    carryWidth = 2;
  }

  if (carryHeight == 0) {
    carryHeight = 2;
  }

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
     * block 32x16 = carryWidth * carryHeight
     * 0 = 1*1     = 1 block
     * 1 = 1*2     = 2 block
     * 2 = 2*1 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 2 && carryHeight == 1) {
      size += 1;
    }

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
     * carryWidth * carryHeight = size block
     * 0 = 1*1     = 1 block
     * 1 = 1*2     = 2 block
     * 2 = 2*1 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 1 && carryHeight == 2) {
      size += 1;
    }
  }

  printf("1 carryWidth: %d\n", carryWidth);
  printf("1 carryHeight: %d\n", carryHeight);

  size += carryWidth * carryHeight + section + totalPages * 32;

  size = size * GS_VRAM_TEXTURE_ALIGNMENT;

  // switch (psm) {
  //   case GS_PSM_4:
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 32x16 pixels.
  //      *  128 pixels
  //      * -------------
  //      * |           |
  //      * |00|02|08|09| -|
  //      * |01|03|10|11|  |
  //      * |04|05|12|13|  |
  //      * |06|07|14|15|  |
  //      * |16|18|24|26|  | 128 pixels
  //      * |17|19|25|27|  |
  //      * |20|22|28|30|  |
  //      * |21|23|29|31| -|
  //      **/

  //     totalWidthPage += width / 128;
  //     totalHeightPage += height / 128;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 128 * (totalWidthPage - 1);
  //     height = height - 128 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 128;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 128;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 32x16 pixels.
  //      * |0|0|2|2|
  //      * |0|0|2|2|
  //      * |1|1|3|3|
  //      * |1|1|3|3|
  //      * |4|4|5|5|
  //      * |4|4|5|5|
  //      * |6|6|7|7|
  //      * |6|6|7|7|
  //      * section * 4 (blocks) = n blocks 32x16
  //      **/

  //     if (width <= 64 && height <= 32) {
  //       section = 0;  // section 0 * 4 blocks 32x16
  //     } else if (width <= 64 && height <= 64) {
  //       section = 4;  // section 1 * 4 blocks 32x16
  //     } else if (width <= 128 && height <= 32) {
  //       section = 8;  // section 2 * 4 blocks 32x16
  //     } else if (width <= 128 && height <= 64) {
  //       section = 12;  // 3 * 4 blocks 32x16
  //     } else if (width <= 64 && height <= 96) {
  //       section = 16;  // 4 * 4 blocks 32x16
  //     } else if (width <= 64 && height <= 128) {
  //       section = 20;  // 5 * 4 blocks 32x16
  //     } else if (width <= 128 && height <= 96) {
  //       section = 24;  // 6 * 4 blocks 32x16
  //     } else if (width <= 128 && height <= 128) {
  //       section = 28;  // 7 * 4 blocks 32x16
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 32;
  //     heightBlock = height / 16;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * block 32x16 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 32x16
  //      * 1 = 1*2     = 2 block 32x16
  //      * 2 = 2*1 + 1 = 3 block 32x16
  //      * 3 = 2*2     = 4 block 32x16
  //      **/

  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     // printf("1 carryWidth: %d\n", carryWidth);
  //     // printf("1 carryHeight: %d\n", carryHeight);

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 2 && carryHeight == 1) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSM_8:
  //     // printf("estoy en 8bpp\n");
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 16x16 pixels.
  //      *        128 pixels
  //      * -------------------------
  //      * |                       |
  //      * |00|01|04|05|16|17|20|21| --
  //      * |02|03|06|07|18|19|22|23| -| 64 pixels
  //      * |08|09|12|13|24|25|28|29| -|
  //      * |10|11|14|15|26|27|30|31| --
  //      **/

  //     totalWidthPage += width / 128;
  //     totalHeightPage += height / 64;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 128 * (totalWidthPage - 1);
  //     height = height - 64 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 128;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 64;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 16x16 pixels.
  //      * |0|0|1|1|4|4|5|5|
  //      * |0|0|1|1|4|4|5|5|
  //      * |2|2|3|3|6|6|7|7|
  //      * |2|2|3|3|6|6|7|7|
  //      * section * 4 (blocks) = n blocks 16x16
  //      **/

  //     if (width <= 32 && height <= 32) {
  //       section = 0;  // section 0 * 4 blocks 16x16
  //     } else if (width <= 64 && height <= 32) {
  //       section = 4;  // section 1 * 4 blocks 16x16
  //     } else if (width <= 32 && height <= 64) {
  //       section = 8;  // section 2 * 4 blocks 16x16
  //     } else if (width <= 64 && height <= 64) {
  //       section = 12;  // 3 * 4 blocks 16x16
  //     } else if (width <= 96 && height <= 32) {
  //       section = 16;  // 4 * 4 blocks 16x16
  //     } else if (width <= 128 && height <= 32) {
  //       section = 20;  // 5 * 4 blocks 16x16
  //     } else if (width <= 96 && height <= 64) {
  //       section = 24;  // 6 * 4 blocks 16x16
  //     } else if (width <= 128 && height <= 64) {
  //       section = 28;  // 7 * 4 blocks 16x16
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 16;
  //     heightBlock = height / 16;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|1|0|1|0|1|0|1|
  //      * |2|3|2|3|2|3|2|3|
  //      * |0|1|0|1|0|1|0|1|
  //      * |2|3|2|3|2|3|2|3|
  //      * block 16x16 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 16x16
  //      * 1 = 1*2     = 2 block 16x16
  //      * 2 = 2*1 + 1 = 3 block 16x16
  //      * 3 = 2*2     = 4 block 16x16
  //      **/

  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     // printf("1 carryWidth: %d\n", carryWidth);
  //     // printf("1 carryHeight: %d\n", carryHeight);

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 1 && carryHeight == 2) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSM_24:
  //   case GS_PSM_32:
  //   case GS_PSM_8H:
  //   case GS_PSM_4HL:
  //   case GS_PSM_4HH:
  //   case GS_PSMZ_24:
  //   case GS_PSMZ_32:
  //     // printf("estoy en 32bpp\n");
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 8x8 pixels.
  //      *        64 pixels
  //      * -------------------------
  //      * |                       |
  //      * |00|01|04|05|16|17|20|21| --
  //      * |02|03|06|07|18|19|22|23| -| 32 pixels
  //      * |08|09|12|13|24|25|28|29| -|
  //      * |10|11|14|15|26|27|30|31| --
  //      **/

  //     totalWidthPage += width / 64;
  //     totalHeightPage += height / 32;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 64 * (totalWidthPage - 1);
  //     height = height - 32 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 64;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 32;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 8x8 pixels.
  //      * |0|0|1|1|4|4|5|5|
  //      * |0|0|1|1|4|4|5|5|
  //      * |2|2|3|3|6|6|7|7|
  //      * |2|2|3|3|6|6|7|7|
  //      * section * 4 (blocks) = n blocks 8x8
  //      **/

  //     if (width <= 16 && height <= 16) {
  //       section = 0;  // section 0 * 4 blocks 8x8
  //     } else if (width <= 32 && height <= 16) {
  //       section = 4;  // section 1 * 4 blocks 8x8
  //     } else if (width <= 16 && height <= 32) {
  //       section = 8;  // section 2 * 4 blocks 8x8
  //     } else if (width <= 32 && height <= 32) {
  //       section = 12;  // 3 * 4 blocks 8x8
  //     } else if (width <= 48 && height <= 16) {
  //       section = 16;  // 4 * 4 blocks 8x8
  //     } else if (width <= 64 && height <= 16) {
  //       section = 20;  // 5 * 4 blocks 8x8
  //     } else if (width <= 48 && height <= 32) {
  //       section = 24;  // 6 * 4 blocks 8x8
  //     } else if (width <= 64 && height <= 32) {
  //       section = 28;  // 7 * 4 blocks 8x8
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 8;
  //     heightBlock = height / 8;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|1|0|1|0|1|0|1|
  //      * |2|3|2|3|2|3|2|3|
  //      * |0|1|0|1|0|1|0|1|
  //      * |2|3|2|3|2|3|2|3|
  //      * block 8x8 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 8x8
  //      * 1 = 1*2     = 2 block 8x8
  //      * 2 = 2*1 + 1 = 3 block 8x8
  //      * 3 = 2*2     = 4 block 8x8
  //      **/
  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0 && heightBlock != 0) {
  //       carryHeight = 2;
  //     }

  //     // printf("1 carryWidth: %d\n", carryWidth);
  //     // printf("1 carryHeight: %d\n", carryHeight);

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 1 && carryHeight == 2) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSM_16:
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 16x8 pixels.
  //      *  64 pixels
  //      * -------------
  //      * |           |
  //      * |00|02|08|09| -|
  //      * |01|03|10|11|  |
  //      * |04|05|12|13|  |
  //      * |06|07|14|15|  |
  //      * |16|18|24|26|  | 64 pixels
  //      * |17|19|25|27|  |
  //      * |20|22|28|30|  |
  //      * |21|23|29|31| -|
  //      **/

  //     totalWidthPage += width / 64;
  //     totalHeightPage += height / 64;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 64 * (totalWidthPage - 1);
  //     height = height - 64 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 64;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 64;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 16x8 pixels.
  //      * |0|0|2|2|
  //      * |0|0|2|2|
  //      * |1|1|3|3|
  //      * |1|1|3|3|
  //      * |4|4|5|5|
  //      * |4|4|5|5|
  //      * |6|6|7|7|
  //      * |6|6|7|7|
  //      * section * 4 (blocks) = n blocks 16x8
  //      **/

  //     if (width <= 32 && height <= 16) {
  //       section = 0;  // section 0 * 4 blocks 16x8
  //     } else if (width <= 32 && height <= 32) {
  //       section = 4;  // section 1 * 4 blocks 16x8
  //     } else if (width <= 64 && height <= 16) {
  //       section = 8;  // section 2 * 4 blocks 16x8
  //     } else if (width <= 64 && height <= 32) {
  //       section = 12;  // 3 * 4 blocks 16x8
  //     } else if (width <= 32 && height <= 48) {
  //       section = 16;  // 4 * 4 blocks 16x8
  //     } else if (width <= 32 && height <= 64) {
  //       section = 20;  // 5 * 4 blocks 16x8
  //     } else if (width <= 64 && height <= 48) {
  //       section = 24;  // 6 * 4 blocks 16x8
  //     } else if (width <= 64 && height <= 64) {
  //       section = 28;  // 7 * 4 blocks 16x8
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 16;
  //     heightBlock = height / 8;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 16x8 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * block 16x8 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 16x8
  //      * 1 = 1*2     = 2 block 16x8
  //      * 2 = 2*1 + 1 = 3 block 16x8
  //      * 3 = 2*2     = 4 block 16x8
  //      **/

  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     // printf("1 carryWidth: %d\n", carryWidth);
  //     // printf("1 carryHeight: %d\n", carryHeight);

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 2 && carryHeight == 1) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSMZ_16:
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 16x8 pixels.
  //      *  64 pixels
  //      * -------------
  //      * |           |
  //      * |00|02|08|09| -|
  //      * |01|03|10|11|  |
  //      * |04|05|12|13|  |
  //      * |06|07|14|15|  |
  //      * |16|18|24|26|  | 64 pixels
  //      * |17|19|25|27|  |
  //      * |20|22|28|30|  |
  //      * |21|23|29|31| -|
  //      **/

  //     totalWidthPage += width / 64;
  //     totalHeightPage += height / 64;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 64 * (totalWidthPage - 1);
  //     height = height - 64 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 64;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 64;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 16x8 pixels.
  //      * |6|6|4|4|
  //      * |6|6|4|4|
  //      * |7|7|5|5|
  //      * |7|7|5|5|
  //      * |2|2|0|0|
  //      * |2|2|0|0|
  //      * |3|3|1|1|
  //      * |3|3|1|1|
  //      * section * 4 (blocks) = n blocks 16x8
  //      **/

  //     if (width <= 32 && height <= 16) {
  //       section = 6 * 4;
  //     } else if (width <= 32 && height <= 32) {
  //       section = 7 * 4;
  //     } else if (width <= 64 && height <= 16) {
  //       section = 4 * 4;
  //     } else if (width <= 64 && height <= 32) {
  //       section = 5 * 4;
  //     } else if (width <= 32 && height <= 48) {
  //       section = 2 * 4;
  //     } else if (width <= 32 && height <= 64) {
  //       section = 3 * 4;
  //     } else if (width <= 64 && height <= 48) {
  //       section = 0;
  //     } else if (width <= 64 && height <= 64) {
  //       section = 1 * 4;
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 16;
  //     heightBlock = height / 8;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 16x8 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * block 16x8 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 16x8
  //      * 1 = 1*2     = 2 block 16x8
  //      * 2 = 2*1 + 1 = 3 block 16x8
  //      * 3 = 2*2     = 4 block 16x8
  //      **/

  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 2 && carryHeight == 1) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSM_16S:
  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 16x8 pixels.
  //      *  64 pixels
  //      * -------------
  //      * |           |
  //      * |00|02|08|09| -|
  //      * |01|03|10|11|  |
  //      * |04|05|12|13|  |
  //      * |06|07|14|15|  |
  //      * |16|18|24|26|  | 64 pixels
  //      * |17|19|25|27|  |
  //      * |20|22|28|30|  |
  //      * |21|23|29|31| -|
  //      **/

  //     totalWidthPage += width / 64;
  //     totalHeightPage += height / 64;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 64 * (totalWidthPage - 1);
  //     height = height - 64 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 64;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 64;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 16x8 pixels.
  //      * |0|0|4|4|
  //      * |0|0|4|4|
  //      * |2|2|6|6|
  //      * |2|2|6|6|
  //      * |1|1|5|5|
  //      * |1|1|5|5|
  //      * |3|3|7|7|
  //      * |3|3|7|7|
  //      * section * 4 (blocks) = n blocks 16x8
  //      **/

  //     if (width <= 32 && height <= 16) {
  //       section = 0;
  //     } else if (width <= 32 && height <= 32) {
  //       section = 2 * 4;
  //     } else if (width <= 64 && height <= 16) {
  //       section = 4 * 4;
  //     } else if (width <= 64 && height <= 32) {
  //       section = 6 * 4;
  //     } else if (width <= 32 && height <= 48) {
  //       section = 1 * 4;
  //     } else if (width <= 32 && height <= 64) {
  //       section = 3 * 4;
  //     } else if (width <= 64 && height <= 48) {
  //       section = 5 * 4;
  //     } else if (width <= 64 && height <= 64) {
  //       section = 7 * 4;
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 16;
  //     heightBlock = height / 8;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 16x8 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * block 16x8 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 16x8
  //      * 1 = 1*2     = 2 block 16x8
  //      * 2 = 2*1 + 1 = 3 block 16x8
  //      * 3 = 2*2     = 4 block 16x8
  //      **/
  //     if (widthBlock == 0) {
  //       widthBlock = 1;
  //     }

  //     if (heightBlock == 0) {
  //       heightBlock = 1;
  //     }

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 2 && carryHeight == 1) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   case GS_PSMZ_16S:

  //     /**
  //      * 1 Page = 32 blocks
  //      * Each block represents 16x8 pixels.
  //      *  64 pixels
  //      * -------------
  //      * |           |
  //      * |00|02|08|09| -|
  //      * |01|03|10|11|  |
  //      * |04|05|12|13|  |
  //      * |06|07|14|15|  |
  //      * |16|18|24|26|  | 64 pixels
  //      * |17|19|25|27|  |
  //      * |20|22|28|30|  |
  //      * |21|23|29|31| -|
  //      **/

  //     totalWidthPage += width / 64;
  //     totalHeightPage += height / 64;

  //     // printf("width: %d\n", width);
  //     // printf("height: %d\n", height);

  //     /**
  //      * Delete 1 page.
  //      * Represents the current page you are working on
  //      * and gets the actual block size
  //      * Here it reduces everything to the first 32 blocks.
  //      * */

  //     width = width - 64 * (totalWidthPage - 1);
  //     height = height - 64 * (totalHeightPage - 1);

  //     /**
  //      * If the width or height is 0.
  //      * It means that it completes the page and does not create another one.
  //      **/

  //     if (width == 0) {
  //       totalWidthPage--;
  //       width = 64;
  //     }

  //     if (height == 0) {
  //       totalHeightPage--;
  //       height = 64;
  //     }

  //     /**
  //      * delete 1 page
  //      * representing the actual page and get the truth block size
  //      **/

  //     totalPages = (totalWidthPage * totalHeightPage) - 1;

  //     if (totalPages < 0) {
  //       totalPages = 0;
  //     }

  //     // printf("page width,heigth: %d,%d\n", totalWidthPage,
  //     totalHeightPage);
  //     // printf("page totalPages: %d\n", totalPages);

  //     // printf("resize width: %d\n", width);
  //     // printf("resize height: %d\n", height);

  //     /**
  //      * This gets the 4x4 block sections of the page that need to be
  //      included.
  //      * Each block represents 16x8 pixels.
  //      * |6|6|2|2|
  //      * |6|6|2|2|
  //      * |4|4|0|0|
  //      * |4|4|0|0|
  //      * |7|7|3|3|
  //      * |7|7|3|3|
  //      * |5|5|1|1|
  //      * |5|5|1|1|
  //      * section * 4 (blocks) = n blocks 16x8
  //      **/

  //     if (width <= 32 && height <= 16) {
  //       section = 6 * 4;
  //     } else if (width <= 32 && height <= 32) {
  //       section = 4 * 4;
  //     } else if (width <= 64 && height <= 16) {
  //       section = 2 * 4;
  //     } else if (width <= 64 && height <= 32) {
  //       section = 0 * 4;
  //     } else if (width <= 32 && height <= 48) {
  //       section = 7 * 4;
  //     } else if (width <= 32 && height <= 64) {
  //       section = 5 * 4;
  //     } else if (width <= 64 && height <= 48) {
  //       section = 4 * 4;
  //     } else if (width <= 64 && height <= 64) {
  //       section = 1 * 4;
  //     }

  //     // printf("Section: %d\n", section);

  //     widthBlock = width / 16;
  //     heightBlock = height / 8;

  //     // printf("1 widthBlock: %d\n", widthBlock);
  //     // printf("1 heightBlock: %d\n", heightBlock);

  //     /**
  //      * This gets the position of the first 4 16x8 blocks.
  //      * knowing if the number of blocks is odd or not.
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * |0|2|0|2|
  //      * |1|3|1|2|
  //      * block 16x8 = carryWidth * carryHeight
  //      * 0 = 1*1     = 1 block 16x8
  //      * 1 = 1*2     = 2 block 16x8
  //      * 2 = 2*1 + 1 = 3 block 16x8
  //      * 3 = 2*2     = 4 block 16x8
  //      **/

  //     carryWidth = widthBlock % 2;
  //     carryHeight = heightBlock % 2;

  //     if (carryWidth == 0) {
  //       carryWidth = 2;
  //     }
  //     if (carryHeight == 0) {
  //       carryHeight = 2;
  //     }

  //     size = carryWidth * carryHeight + section + totalPages * 32;

  //     if (carryWidth == 2 && carryHeight == 1) {
  //       size += 1;
  //     }

  //     size = size * GS_VRAM_TEXTURE_ALIGNMENT;
  //     break;
  //   default:
  //     return 0;
  // }

  // printf("size without alignment: %d\n", size);

  size = -alignment & (size + (alignment - 1));

  // printf("final size alignment: %d\n", size);

  return size;
}

}  // namespace Tyra
