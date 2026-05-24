/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#pragma once

#include "debug/debug.hpp"
#include "renderer/core/2d/sprite/sprite.hpp"
#include "renderer/core/texture/renderer_core_texture_buffers.hpp"
#include "renderer/3d/pipeline/shared/pipeline_texture_mapping_type.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include "renderer/renderer_settings.hpp"
#include <packet2_utils.h>
#include <draw2d.h>
#include <draw_tests.h>

namespace Tyra {

class RendererCore2D {
 public:
  RendererCore2D();
  ~RendererCore2D();

  void init(RendererSettings* settings, clutbuffer_t* clutBuffer);

  void render(const Sprite& sprite,
              const RendererCoreTextureBuffers& texBuffers, Texture* texture);
  void renderSprite(const Vec2 position, const Vec2 offset,
                                  const Vec2 size, const float scale,
                                  Tyra::SpriteMode mode, bool flipX, bool flipY,
                                  Color color,
                                  const RendererCoreTextureBuffers& texBuffers,
                                  Texture* texture);
  void renderSpriteRotate(const Vec2 position, const Vec2 offset, const Vec2 size,
                    const float scale, float rotation, Tyra::SpriteMode mode, bool flipX,
                    bool flipY, Color color,
                    const RendererCoreTextureBuffers& texBuffers,
                    Texture* texture);
  void renderRotate(const Sprite& sprite,
                    const RendererCoreTextureBuffers& texBuffers,
                    Texture* texture, const Vec2& angle);

  void setTextureMappingType(
      const PipelineTextureMappingType textureMappingType);

  /**
   * Controls which pixels to draw with the Alpha test, Destination Test and
   * Z/Depth Test.
   *
   * Not change pixel Values and doesn't affect subsequent processes, like the
   * Alpha-blending. The pixels are controlled when the pixel is written in the
   * frame buffer and the Z buffer.
   */
  void pixelTest(atest_t* alpha, dtest_t* destAlpha, unsigned char zMethod);

 private:
  void setPrim();
  void setLod();

  prim_t prim;
  lod_t lod;
  ztest_t ztest;

  static const float GS_DRAW_AREA;
  static const float SCREEN_CENTER;

  u8 context;
  RendererSettings* settings;
  clutbuffer_t* clutBuffer;
  packet2_t* packets[2];
  texrect_t* rects[2];
};

}  // namespace Tyra
