/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/core/2d/renderer_core_2d.hpp"
#include "math/math.hpp"
#include <dma.h>
#include <draw.h>

namespace Tyra {

RendererCore2D::RendererCore2D() {
  context = 0;
  packets[0] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packets[1] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  rects[0] = new texrect_t;
  rects[1] = new texrect_t;
  ztest.enable = DRAW_ENABLE;

  setPrim();
  setLod();
}

RendererCore2D::~RendererCore2D() {
  packet2_free(packets[0]);
  packet2_free(packets[1]);
  delete rects[0];
  delete rects[1];
}

const float RendererCore2D::GS_DRAW_AREA = 4096.0F;
const float RendererCore2D::SCREEN_CENTER = 4096.0F / 2.0F;

void RendererCore2D::setPrim() {
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;
}

void RendererCore2D::setLod() {
  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_LINEAR;
  lod.min_filter = LOD_MIN_LINEAR;
  lod.mipmap_select = LOD_MIPMAP_REGISTER;
  lod.l = 0;
  lod.k = 0.0F;
}

void RendererCore2D::init(RendererSettings* t_settings,
                          clutbuffer_t* t_clutBuffer) {
  settings = t_settings;
  clutBuffer = t_clutBuffer;
}

void RendererCore2D::render(const Sprite& sprite,
                            const RendererCoreTextureBuffers& texBuffers,
                            Texture* texture) {
  auto* rect = rects[context];
  float sizeX, sizeY;

  if (sprite.mode == MODE_REPEAT) {
    sizeX = sprite.size.x;
    sizeY = sprite.size.y;
  } else {
    sizeX = static_cast<float>(texture->getWidth());
    sizeY = static_cast<float>(texture->getHeight());
  }

  float texS, texT;
  float texMax = texT = texS = sizeX > sizeY ? sizeX : sizeY;

  if (sizeX > sizeY)
    texT = texMax / (sizeX / sizeY);
  else if (sizeY > sizeX)
    texS = texMax / (sizeY / sizeX);

  rect->t0.s =
      sprite.flipHorizontal ? (texS + sprite.offset.x) : sprite.offset.x;
  rect->t0.t = sprite.flipVertical ? (texT + sprite.offset.y) : sprite.offset.y;
  rect->t1.s =
      sprite.flipHorizontal ? sprite.offset.x : (texS + sprite.offset.x);
  rect->t1.t = sprite.flipVertical ? sprite.offset.y : (texT + sprite.offset.y);

  rect->color.r = sprite.color.r;
  rect->color.g = sprite.color.g;
  rect->color.b = sprite.color.b;
  rect->color.a = sprite.color.a;
  rect->color.q = 0;

  rect->v0.x = sprite.position.x;
  rect->v0.y = sprite.position.y;
  // rect->v0.y /= 2.0F;  // interlacing
  rect->v0.z = (u32)-1;

  rect->v1.x = (sprite.size.x * sprite.scale) + sprite.position.x;
  rect->v1.y = (sprite.size.y * sprite.scale) + sprite.position.y;
  // rect->v1.y /= 2.0F;  // interlacing
  rect->v1.z = (u32)-1;

  auto* packet = packets[context];

  packet2_reset(packet, false);
  packet2_update(packet, draw_primitive_xyoffset(packet->base, 0, SCREEN_CENTER,
                                                 SCREEN_CENTER));

  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_lod(packet, &lod);
  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, clutBuffer);
  draw_enable_blending();
  packet2_update(packet, draw_rect_textured(packet->next, 0, rect));

  packet2_update(packet, draw_primitive_xyoffset(
                             packet->next, 0,
                             SCREEN_CENTER - (settings->getWidth() / 2.0F),
                             SCREEN_CENTER - (settings->getHeight() / 2.0F)));
  draw_disable_blending();
  packet2_update(packet, draw_finish(packet->next));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

void RendererCore2D::renderSprite(const Vec2 position, const Vec2 offset,
                                  const Vec2 size, const float scale,
                                  Tyra::SpriteMode mode, bool flipX, bool flipY,
                                  Color color,
                                  const RendererCoreTextureBuffers& texBuffers,
                                  Texture* texture) {
  auto* rect = rects[context];
  float sizeX, sizeY;

  if (mode == MODE_REPEAT) {
    sizeX = size.x;
    sizeY = size.y;
  } else {
    sizeX = static_cast<float>(texture->getWidth());
    sizeY = static_cast<float>(texture->getHeight());
  }

  float texS, texT;
  float texMax = texT = texS = sizeX > sizeY ? sizeX : sizeY;

  if (sizeX > sizeY)
    texT = texMax / (sizeX / sizeY);
  else if (sizeY > sizeX)
    texS = texMax / (sizeY / sizeX);

  rect->t0.s = flipX ? (texS + offset.x) : offset.x;
  rect->t0.t = flipY ? (texT + offset.y) : offset.y;
  rect->t1.s = flipX ? offset.x : (texS + offset.x);
  rect->t1.t = flipY ? offset.y : (texT + offset.y);

  rect->color.r = color.r;
  rect->color.g = color.g;
  rect->color.b = color.b;
  rect->color.a = color.a;
  rect->color.q = 0;

  rect->v0.x = position.x;
  rect->v0.y = position.y;
  // rect->v0.y /= 2.0F;  // interlacing
  rect->v0.z = (u32)-1;

  rect->v1.x = (size.x * scale) + position.x;
  rect->v1.y = (size.y * scale) + position.y;
  // rect->v1.y /= 2.0F;  // interlacing
  rect->v1.z = (u32)-1;

  auto* packet = packets[context];

  packet2_reset(packet, false);
  packet2_update(packet, draw_primitive_xyoffset(packet->base, 0, SCREEN_CENTER,
                                                 SCREEN_CENTER));

  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_lod(packet, &lod);
  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, clutBuffer);
  draw_enable_blending();
  packet2_update(packet, draw_rect_textured(packet->next, 0, rect));

  packet2_update(packet, draw_primitive_xyoffset(
                             packet->next, 0,
                             SCREEN_CENTER - (settings->getWidth() / 2.0F),
                             SCREEN_CENTER - (settings->getHeight() / 2.0F)));
  draw_disable_blending();
  packet2_update(packet, draw_finish(packet->next));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

#define START_OFFSET 2047.5625f

#define END_OFFSET 2048.5625f

#define TYRA_DRAW_SPRITE_TEX__NREG 10
#define TYRA_DRAW_SPRITE_TEX_REGLIST                        \
  ((u64)GIF_REG_PRIM) << 0 | ((u64)GIF_REG_RGBAQ) << 4 |    \
      ((u64)GIF_REG_UV) << 8 | ((u64)GIF_REG_XYZ2) << 12 |  \
      ((u64)GIF_REG_UV) << 16 | ((u64)GIF_REG_XYZ2) << 20 | \
      ((u64)GIF_REG_UV) << 24 | ((u64)GIF_REG_XYZ2) << 28 | \
      ((u64)GIF_REG_UV) << 32 | ((u64)GIF_REG_XYZ2) << 36

qword_t* tyra_draw_sprite_rotate(qword_t* q, int context, texrect_t* rect,
                                 texrect_t* rect2) {
  // Start primitive
  PACK_GIFTAG(
      q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_REGLIST, TYRA_DRAW_SPRITE_TEX__NREG),
      TYRA_DRAW_SPRITE_TEX_REGLIST);
  q++;

  // Fill vertex information
  q->dw[0] = GIF_SET_PRIM(PRIM_TRIANGLE_STRIP, 0, DRAW_ENABLE, 0, 1, 0,
                          PRIM_MAP_UV, context, 0);
  q->dw[1] = rect->color.rgbaq;
  q++;

  // vertex 1
  q->dw[0] = GIF_SET_UV(ftoi4(rect->t0.u), ftoi4(rect->t0.v));
  q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v0.x + START_OFFSET),
                         ftoi4(rect->v0.y + START_OFFSET), rect->v0.z);
  q++;

  // vertex 2
  q->dw[0] = GIF_SET_UV(ftoi4(rect->t1.u), ftoi4(rect->t1.v));
  q->dw[1] = GIF_SET_XYZ(ftoi4(rect->v1.x + START_OFFSET),
                         ftoi4(rect->v1.y + END_OFFSET), rect->v0.z);
  q++;

  // vertex 3
  q->dw[0] = GIF_SET_UV(ftoi4(rect2->t0.u), ftoi4(rect2->t0.v));
  q->dw[1] = GIF_SET_XYZ(ftoi4(rect2->v0.x + END_OFFSET),
                         ftoi4(rect2->v0.y + START_OFFSET), rect2->v0.z);
  q++;

  // vertex 4
  q->dw[0] = GIF_SET_UV(ftoi4(rect2->t1.u), ftoi4(rect2->t1.v));
  q->dw[1] = GIF_SET_XYZ(ftoi4(rect2->v1.x + END_OFFSET),
                         ftoi4(rect2->v1.y + END_OFFSET), rect2->v1.z);
  q++;

  return q;
}

void RendererCore2D::renderRotate(const Sprite& sprite,
                                  const RendererCoreTextureBuffers& texBuffers,
                                  Texture* texture, const Vec2& angle) {
  auto* rect = rects[context];
  texrect_t rect2;
  float sizeX, sizeY;

  float angleCos = Math::cos(angle.x * Math::ANG2RAD);
  float angleSin = Math::sin(angle.x * Math::ANG2RAD);
  float angleCos2 = Math::cos(angle.y * Math::ANG2RAD);
  float angleSin2 = Math::sin(angle.y * Math::ANG2RAD);

  if (sprite.mode == MODE_REPEAT) {
    sizeX = sprite.size.x;
    sizeY = sprite.size.y;
  } else {
    sizeX = static_cast<float>(texture->getWidth());
    sizeY = static_cast<float>(texture->getHeight());
  }

  float texS, texT;
  float texMax = texT = texS = sizeX > sizeY ? sizeX : sizeY;

  if (sizeX > sizeY)
    texT = texMax / (sizeX / sizeY);
  else if (sizeY > sizeX)
    texS = texMax / (sizeY / sizeX);

  rect->t0.s =
      sprite.flipHorizontal ? (texS + sprite.offset.x) : sprite.offset.x;
  rect->t0.t = sprite.flipVertical ? (texT + sprite.offset.y) : sprite.offset.y;
  rect->t1.s =
      sprite.flipHorizontal ? (texS + sprite.offset.x) : sprite.offset.x;
  rect->t1.t = sprite.flipVertical ? sprite.offset.y : (texT + sprite.offset.y);

  rect2.t0.s =
      sprite.flipHorizontal ? sprite.offset.x : (texS + sprite.offset.x);
  rect2.t0.t = sprite.flipVertical ? (texT + sprite.offset.y) : sprite.offset.y;
  rect2.t1.s =
      sprite.flipHorizontal ? sprite.offset.x : (texS + sprite.offset.x);
  rect2.t1.t = sprite.flipVertical ? sprite.offset.y : (texT + sprite.offset.y);

  rect->color.r = sprite.color.r;
  rect->color.g = sprite.color.g;
  rect->color.b = sprite.color.b;
  rect->color.a = sprite.color.a;
  rect->color.q = 0;

  Vec2 sizeScaled =
      Vec2(sprite.size.x * sprite.scale, sprite.size.y * sprite.scale);

  // Top Left
  rect->v0.x = sprite.position.x;
  rect->v0.y = sprite.position.y;
  rect->v0.z = (u32)-1;

  // Bottom Left
  rect->v1.x = sprite.position.x + sizeScaled.y * (-angleSin2);
  rect->v1.y = sprite.position.y + sizeScaled.y * angleCos2;
  rect->v1.z = (u32)-1;

  // Top Right
  rect2.v0.x = sprite.position.x + sizeScaled.x * angleCos;
  rect2.v0.y = sprite.position.y + sizeScaled.x * angleSin;
  rect2.v0.z = (u32)-1;

  // Bottom Right
  rect2.v1.x =
      sprite.position.x + sizeScaled.x * angleCos + sizeScaled.y * (-angleSin2);
  rect2.v1.y =
      sprite.position.y + sizeScaled.x * angleSin + sizeScaled.y * angleCos2;
  rect2.v1.z = (u32)-1;

  auto* packet = packets[context];

  packet2_reset(packet, false);
  packet2_update(packet, draw_primitive_xyoffset(packet->base, 0, SCREEN_CENTER,
                                                 SCREEN_CENTER));

  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_lod(packet, &lod);
  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, clutBuffer);
  draw_enable_blending();
  packet2_update(packet,
                 tyra_draw_sprite_rotate(packet->next, 0, rect, &rect2));

  packet2_update(packet, draw_primitive_xyoffset(
                             packet->next, 0,
                             SCREEN_CENTER - (settings->getWidth() / 2.0F),
                             SCREEN_CENTER - (settings->getHeight() / 2.0F)));
  draw_disable_blending();
  packet2_update(packet, draw_finish(packet->next));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

void RendererCore2D::renderSpriteRotate(const Vec2 position, const Vec2 offset, const Vec2 size,
                    const float scale, float rotation, Tyra::SpriteMode mode, bool flipX,
                    bool flipY, Color color,
                    const RendererCoreTextureBuffers& texBuffers,
                    Texture* texture) {
  auto* rect = rects[context];
  texrect_t rect2;
  float sizeX, sizeY;

  float angleCos = Math::cos(rotation * Math::ANG2RAD);
  float angleSin = Math::sin(rotation * Math::ANG2RAD);

  if (mode == MODE_REPEAT) {
    sizeX = size.x;
    sizeY = size.y;
  } else {
    sizeX = static_cast<float>(texture->getWidth());
    sizeY = static_cast<float>(texture->getHeight());
  }

  float texS, texT;
  float texMax = texT = texS = sizeX > sizeY ? sizeX : sizeY;

  if (sizeX > sizeY)
    texT = texMax / (sizeX / sizeY);
  else if (sizeY > sizeX)
    texS = texMax / (sizeY / sizeX);

  rect->t0.s =
      flipX ? (texS + offset.x) : offset.x;
  rect->t0.t = flipY ? (texT + offset.y) : offset.y;
  rect->t1.s = flipX ? (texS + offset.x) : offset.x;
  rect->t1.t = flipY ? offset.y : (texT + offset.y);

  rect2.t0.s =
      flipX ? offset.x : (texS + offset.x);
  rect2.t0.t = flipY ? (texT + offset.y) : offset.y;
  rect2.t1.s =
      flipX ? offset.x : (texS + offset.x);
  rect2.t1.t = flipY ? offset.y : (texT + offset.y);

  rect->color.r = color.r;
  rect->color.g = color.g;
  rect->color.b = color.b;
  rect->color.a = color.a;
  rect->color.q = 0;

  Vec2 sizeScaled =
      Vec2(size.x * scale, size.y * scale);

  // Top Left
  rect->v0.x = position.x;
  rect->v0.y = position.y;
  rect->v0.z = (u32)-1;

  // Bottom Left
  rect->v1.x = position.x + sizeScaled.y * (-angleSin);
  rect->v1.y = position.y + sizeScaled.y * angleCos;
  rect->v1.z = (u32)-1;

  // Top Right
  rect2.v0.x = position.x + sizeScaled.x * angleCos;
  rect2.v0.y = position.y + sizeScaled.x * angleSin;
  rect2.v0.z = (u32)-1;

  // Bottom Right
  rect2.v1.x = position.x + sizeScaled.x * angleCos + sizeScaled.y * (-angleSin);
  rect2.v1.y = position.y + sizeScaled.x * angleSin + sizeScaled.y * angleCos;
  rect2.v1.z = (u32)-1;

  auto* packet = packets[context];

  packet2_reset(packet, false);
  packet2_update(packet, draw_primitive_xyoffset(packet->base, 0, SCREEN_CENTER,
                                                 SCREEN_CENTER));

  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_lod(packet, &lod);
  packet2_utils_gif_add_set(packet, 1);
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, clutBuffer);
  draw_enable_blending();
  packet2_update(packet,
                 tyra_draw_sprite_rotate(packet->next, 0, rect, &rect2));

  packet2_update(packet, draw_primitive_xyoffset(
                             packet->next, 0,
                             SCREEN_CENTER - (settings->getWidth() / 2.0F),
                             SCREEN_CENTER - (settings->getHeight() / 2.0F)));
  draw_disable_blending();
  packet2_update(packet, draw_finish(packet->next));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

void RendererCore2D::pixelTest(atest_t* alpha, dtest_t* destAlpha,
                               unsigned char zMethod) {
  auto* packet = packets[context];
  packet2_reset(packet, false);

  ztest.method = zMethod;
  packet2_update(packet,
                 draw_pixel_test(packet->next, 0, alpha, destAlpha, &ztest));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);
}

void RendererCore2D::setTextureMappingType(
    const PipelineTextureMappingType textureMappingType) {
  lod.mag_filter = textureMappingType;
  lod.min_filter = textureMappingType;
}

}  // namespace Tyra
