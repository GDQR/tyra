/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include <iomanip>
#include <draw_buffers.h>
#include <gs_psm.h>
#include <graph_vram.h>
#include <stdlib.h>
#include <fastmath.h>
#include <string>
#include <sstream>
#include "renderer/core/texture/models/texture.hpp"

namespace Tyra {

namespace TyraTexture {
u32 textureCounter = 1;
std::vector<u32> deletedIDs;
}  // namespace TyraTexture

Texture::Texture(TextureBuilderData* t_data) {
  if (TyraTexture::deletedIDs.empty() == false) {
    id = TyraTexture::deletedIDs.front();
    TyraTexture::deletedIDs.erase(TyraTexture::deletedIDs.begin());
  } else {
    id = TyraTexture::textureCounter++;
  }

  name = t_data->name;

  // TYRA_ASSERT(t_data->width <= 512 && t_data->height <= 512,
  //             "Tyra supports only 512x512 textures max!");

  // TYRA_ASSERT(t_data->width == 8 || t_data->width == 16 ||
  //                 t_data->width == 32 || t_data->width == 64 ||
  //                 t_data->width == 128 || t_data->width == 256 ||
  //                 t_data->width == 512,
  //             "Texture width/height should be 8/16/32/64/128/256/512!");

  // TYRA_ASSERT(t_data->height == 8 || t_data->height == 16 ||
  //                 t_data->height == 32 || t_data->height == 64 ||
  //                 t_data->height == 128 || t_data->height == 256 ||
  //                 t_data->height == 512,
  //             "Texture width/height should be 8/16/32/64/128/256/512!");

  core = new TextureData(t_data->data, t_data->bpp, t_data->gsComponents,
                         t_data->width, t_data->height);

  clut =
      new TextureData(t_data->clut, t_data->clutBpp, t_data->clutGsComponents,
                      t_data->clutWidth, t_data->clutHeight);

  setDefaultWrapSettings();
}

Texture::~Texture() {
  TyraTexture::deletedIDs.push_back(id);
  if (links.size() > 0) links.clear();
  if (core) delete core;
  if (clut) delete clut;
}

const s32 Texture::getIndexOfLink(const u32& t_id) const {
  for (u32 i = 0; i < links.size(); i++)
    if (links[i].id == t_id) return i;
  return -1;
}

const u8 Texture::isLinkedWith(const u32& t_id) const {
  s32 index = getIndexOfLink(t_id);
  if (index != -1)
    return true;
  else
    return false;
}

void Texture::removeLinkByIndex(const u32& t_index) {
  links.erase(links.begin() + t_index);
}

void Texture::removeLinkById(const u32& t_id) {
  s32 index = getIndexOfLink(t_id);
  TYRA_ASSERT(index != -1, "Cant remove link, because it was not found!");
  removeLinkByIndex(index);
}

float Texture::getSizeInMB() const {
  return (core->width / 100.0F) * (core->height / 100.0F) *
         (core->bpp / 100.0F) / 8.0F;
}

u32 Texture::getTextureSize() const {
  int width = core->width;
  int height = core->height;

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

  if (core->psm == GS_PSM_4) {
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
  } else if (core->psm == GS_PSM_8) {
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
  } else if (core->psm == GS_PSM_24 || core->psm == GS_PSM_32 ||
             core->psm == GS_PSM_8H || core->psm == GS_PSM_4HL ||
             core->psm == GS_PSM_4HH || core->psm == GS_PSMZ_24 ||
             core->psm == GS_PSMZ_32) {
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
  } else if (core->psm == GS_PSM_16 || core->psm == GS_PSMZ_16 ||
             core->psm == GS_PSM_16S || core->psm == GS_PSMZ_16S) {
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

  if (core->psm == GS_PSM_4 || core->psm == GS_PSM_16) {
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
  } else if (core->psm == GS_PSM_8 || core->psm == GS_PSM_24 ||
             core->psm == GS_PSM_32 || core->psm == GS_PSM_8H ||
             core->psm == GS_PSM_4HL || core->psm == GS_PSM_4HH ||
             core->psm == GS_PSMZ_24 || core->psm == GS_PSMZ_32) {
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
  } else if (core->psm == GS_PSMZ_16) {
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
  } else if (core->psm == GS_PSM_16S) {
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
  } else if (core->psm == GS_PSMZ_16S) {
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

  if (carryWidth == 0) {
    carryWidth = 2;
  }

  if (carryHeight == 0) {
    carryHeight = 2;
  }

  if (core->psm == GS_PSM_4 || core->psm == GS_PSM_16 ||
      core->psm == GS_PSMZ_16 || core->psm == GS_PSM_16S ||
      core->psm == GS_PSMZ_16S) {
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
     * carryWidth * carryHeight = size block
     * 0 = 1*1     = 1 block
     * 1 = 1*2     = 2 block
     * 2 = 2*1 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 2 && carryHeight == 1) {
      size += 1;
    }

  } else if (core->psm == GS_PSM_8 || core->psm == GS_PSM_24 ||
             core->psm == GS_PSM_32 || core->psm == GS_PSM_8H ||
             core->psm == GS_PSM_4HL || core->psm == GS_PSM_4HH ||
             core->psm == GS_PSMZ_24 || core->psm == GS_PSMZ_32) {
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
     * 2 = 1*2 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 1 && carryHeight == 2) {
      size += 1;
    }
  }

  size += carryWidth * carryHeight;

  size *= GRAPH_ALIGN_BLOCK;

  size = -GRAPH_ALIGN_BLOCK & (size + (GRAPH_ALIGN_BLOCK - 1));

  return size;
}

u32 Texture::getClutTextureSize() const {
  int width = clut->width;
  int height = clut->height;

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

  if (clut->psm == GS_PSM_4) {
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
  } else if (clut->psm == GS_PSM_8) {
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
  } else if (clut->psm == GS_PSM_24 || clut->psm == GS_PSM_32 ||
             clut->psm == GS_PSM_8H || clut->psm == GS_PSM_4HL ||
             clut->psm == GS_PSM_4HH || clut->psm == GS_PSMZ_24 ||
             clut->psm == GS_PSMZ_32) {
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
  } else if (clut->psm == GS_PSM_16 || clut->psm == GS_PSMZ_16 ||
             clut->psm == GS_PSM_16S || clut->psm == GS_PSMZ_16S) {
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

  if (clut->psm == GS_PSM_4 || clut->psm == GS_PSM_16) {
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
  } else if (clut->psm == GS_PSM_8 || clut->psm == GS_PSM_24 ||
             clut->psm == GS_PSM_32 || clut->psm == GS_PSM_8H ||
             clut->psm == GS_PSM_4HL || clut->psm == GS_PSM_4HH ||
             clut->psm == GS_PSMZ_24 || clut->psm == GS_PSMZ_32) {
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
  } else if (clut->psm == GS_PSMZ_16) {
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
  } else if (clut->psm == GS_PSM_16S) {
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
  } else if (clut->psm == GS_PSMZ_16S) {
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

  if (carryWidth == 0) {
    carryWidth = 2;
  }

  if (carryHeight == 0) {
    carryHeight = 2;
  }

  if (clut->psm == GS_PSM_4 || clut->psm == GS_PSM_16 ||
      clut->psm == GS_PSMZ_16 || clut->psm == GS_PSM_16S ||
      clut->psm == GS_PSMZ_16S) {
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
     * carryWidth * carryHeight = size block
     * 0 = 1*1     = 1 block
     * 1 = 1*2     = 2 block
     * 2 = 2*1 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 2 && carryHeight == 1) {
      size += 1;
    }

  } else if (clut->psm == GS_PSM_8 || clut->psm == GS_PSM_24 ||
             clut->psm == GS_PSM_32 || clut->psm == GS_PSM_8H ||
             clut->psm == GS_PSM_4HL || clut->psm == GS_PSM_4HH ||
             clut->psm == GS_PSMZ_24 || clut->psm == GS_PSMZ_32) {
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
     * 2 = 1*2 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 1 && carryHeight == 2) {
      size += 1;
    }
  }

  size += carryWidth * carryHeight;

  size *= GRAPH_ALIGN_BLOCK;

  size = -GRAPH_ALIGN_BLOCK & (size + (GRAPH_ALIGN_BLOCK - 1));

  return size;
}

void Texture::setDefaultWrapSettings() {
  wrap.horizontal = WRAP_REPEAT;
  wrap.vertical = WRAP_REPEAT;
  wrap.maxu = 0;
  wrap.maxv = 0;
  wrap.minu = 0;
  wrap.minv = 0;
}

void Texture::setWrapSettings(const TextureWrap t_horizontal,
                              const TextureWrap t_vertical, int minu, int minv,
                              int maxu, int maxv) {
  wrap.horizontal = t_horizontal;
  wrap.vertical = t_vertical;
  wrap.minu = minu;
  wrap.minv = minv;
  wrap.maxu = maxu;
  wrap.maxv = maxv;
}

void Texture::addLink(const u32& t_id) {
  TextureLink link;
  link.id = t_id;
  links.push_back(link);
}

void Texture::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void Texture::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string Texture::getPrint(const char* objectName) const {
  std::stringstream res;
  if (objectName) {
    res << objectName << "(";
  } else {
    res << "Texture(";
  }

  std::string wrapString;
  if (wrap.horizontal == WRAP_REPEAT)
    wrapString = "WRAP_REPEAT";
  else if (wrap.horizontal == WRAP_CLAMP)
    wrapString = "WRAP_CLAMP";

  res << "id: " << id << ", " << std::endl;
  res << "name: " << name << ", " << std::endl;
  res << "core: " << core->getPrint() << ", " << std::endl;
  if (clut != nullptr) res << "clut: " << clut->getPrint() << ", " << std::endl;
  if (links.size()) {
    for (size_t i = 0; i < links.size(); i++) {
      res << "link " << i << " for id: " << links[i].id << ", " << std::endl;
    }
  }
  res << "wrap: " << wrapString << ")";
  return res.str();
}

}  // namespace Tyra
