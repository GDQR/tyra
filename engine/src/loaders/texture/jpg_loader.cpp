/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2024, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Guido Diego Quispe Robles
# André Guilherme <andregui17@outlook.com>
*/

#include "debug/debug.hpp"
#include <cstdio>
#include <malloc.h>
#include <jpeglib.h>
#include <cstring>
#include <draw_buffers.h>
#include "loaders/texture/jpg_loader.hpp"
#include "file/file_utils.hpp"
#include <csetjmp>
#include <cmath>

namespace Tyra {
JpgLoader::JpgLoader() {}

JpgLoader::~JpgLoader() {}

struct my_error_mgr {
  struct jpeg_error_mgr pub; /* "public" fields */

  jmp_buf setjmp_buffer; /* for return to caller */
};

typedef struct my_error_mgr* my_error_ptr;

METHODDEF(void)
my_error_exit(j_common_ptr cinfo) {
  /* cinfo->err really points to a my_error_mgr struct, so coerce pointer */
  my_error_ptr myerr = (my_error_ptr)cinfo->err;

  /* Always display the message. */
  /* We could postpone this until after returning, if we chose. */
  (*cinfo->err->output_message)(cinfo);

  /* Return control to the setjmp point */
  longjmp(myerr->setjmp_buffer, 1);
}

/** Based on GsKit texture loading - thank you guys! */
TextureBuilderData* JpgLoader::load(const char* fullPath) {
  std::string path = fullPath;
  TYRA_ASSERT(!path.empty(), "Provided path is empty!");

  auto filename = FileUtils::getFilenameFromPath(path);

  FILE* file = fopen(fullPath, "rb");
  TYRA_ASSERT(file != nullptr, "Failed to load ", fullPath);

  struct jpeg_decompress_struct cinfo;
  struct my_error_mgr jerr;

  /* We set up the normal JPEG error routines, then override error_exit. */
  cinfo.err = jpeg_std_error(&jerr.pub);
  jerr.pub.error_exit = my_error_exit;
  /* Establish the setjmp return context for my_error_exit to use. */

  if (setjmp(jerr.setjmp_buffer)) {
    /* If we get here, the JPEG code has signaled an error.
     * We need to clean up the JPEG object, close the input file, and return.
     */
    jpeg_destroy_decompress(&cinfo);
    fclose(file);
    TYRA_ASSERT(!setjmp(jerr.setjmp_buffer),
                "jpeg: error during processing file");
  }
  jpeg_create_decompress(&cinfo);
  jpeg_stdio_src(&cinfo, file);
  jpeg_read_header(&cinfo, TRUE);

  jpeg_start_decompress(&cinfo);

  auto* result = new TextureBuilderData();
  result->width = cinfo.output_width;
  result->height = cinfo.output_height;
  result->name = filename;
  result->gsComponents = TEXTURE_COMPONENTS_RGB;

  // if (result->width == 8) {
  //   result->width = 8;
  // } else if (result->width <= 16) {
  //   result->width = 16;
  // } else if (result->width <= 32) {
  //   result->width = 32;
  // } else if (result->width <= 64) {
  //   result->width = 64;
  // } else if (result->width <= 128) {
  //   result->width = 128;
  // } else if (result->width <= 256) {
  //   result->width = 256;
  // } else if (result->width <= 512) {
  //   result->width = 512;
  // }else{
  //   TYRA_TRAP("Texture width is greater than 512");
  // }
  

  // if (result->height == 8) {
  //   result->height = 8;
  // } else if (result->height <= 16) {
  //   result->height = 16;
  // } else if (result->height <= 32) {
  //   result->height = 32;
  // } else if (result->height <= 64) {
  //   result->height = 64;
  // } else if (result->height <= 128) {
  //   result->height = 128;
  // } else if (result->height <= 256) {
  //   result->height = 256;
  // } else if (result->height <= 512) {
  //   result->height = 512;
  // }else{
  //   TYRA_TRAP("Texture height is greater than 512");
  // }

  int textureSize;

  if (cinfo.out_color_components == JCS_YCbCr) {
    result->bpp = bpp24;
    textureSize = getTextureSize(result->width, result->height, bpp24);

    result->data = static_cast<unsigned char*>(memalign(128, textureSize));
    memset(result->data, 0, textureSize);

    unsigned int row_stride = textureSize / result->height;
    unsigned char* row_pointer = result->data;

    while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&row_pointer, 1);
      row_pointer += row_stride;
    }
  } else if (cinfo.out_color_components == JCS_CMYK) {
    result->bpp = bpp32;
    textureSize = getTextureSize(result->width, result->height, bpp32);

    result->data = static_cast<unsigned char*>(memalign(128, textureSize));
    memset(result->data, 0, textureSize);

    unsigned int row_stride = textureSize / result->height;
    unsigned char* row_pointer = result->data;

    while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&row_pointer, 1);
      row_pointer += row_stride;
    }
  } else if (cinfo.out_color_components == JCS_GRAYSCALE) {
    /**
     * This color of 8 bits is converted to 24 bits
     */
    result->bpp = bpp24;
    textureSize = getTextureSize(result->width, result->height, bpp8);

    result->data = static_cast<unsigned char*>(memalign(128, textureSize));
    memset(result->data, 0, textureSize);

    unsigned int row_stride = textureSize / result->height;
    unsigned char* row_pointer = result->data;

    while (cinfo.output_scanline < cinfo.output_height) {
      jpeg_read_scanlines(&cinfo, (JSAMPARRAY)&row_pointer, 1);
      row_pointer += row_stride;
    }

    unsigned char* newArray = static_cast<unsigned char*>(
        memalign(128, getTextureSize(result->width, result->height, bpp24)));
    int u = 0;
    for (int i = 0; i < result->height * result->width; i++) {
      newArray[u++] = result->data[i];
      newArray[u++] = result->data[i];
      newArray[u++] = result->data[i];
    }
    delete result->data;
    result->data = newArray;
  } else
    TYRA_TRAP("This texture depth is not supported!");

  jpeg_finish_decompress(&cinfo);

  jpeg_destroy_decompress(&cinfo);
  fclose(file);

  return result;
}

}  // namespace Tyra