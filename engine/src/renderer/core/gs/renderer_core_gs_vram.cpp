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
#include "engine.hpp"
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

float RendererCoreGSVRam::getSizeInMB(int width, const int& height,
                                      const int& psm, const int& alignment) {
  return GetVramSize(width, height, psm, alignment) / ptr2MB;
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
  auto size = GetVramSize(width, height, psm, alignment);

  pointer += size;

  // If the pointer overflows the vram size
  if (pointer > GRAPH_VRAM_MAX_WORDS) {
    pointer -= size;
    return -1;
  }

  touched = true;

  return pointer - size;
}

void RendererCoreGSVRam::free(const int& address) {
  pointer = address;
  touched = true;
}

}  // namespace Tyra
