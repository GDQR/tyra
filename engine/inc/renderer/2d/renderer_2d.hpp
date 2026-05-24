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

#include "renderer/core/2d/sprite/sprite.hpp"
#include "renderer/core/renderer_core.hpp"

namespace Tyra {

class Renderer2D {
 public:
  Renderer2D();
  ~Renderer2D();

  void init(RendererCore* rendererCore);
  void render(const Sprite* sprite);
  void render(const Sprite& sprite);
  void renderSprite(u32 textureID, float x, float y, Tyra::Vec2 offset, Tyra::Vec2 size, float scale, float rotation, Tyra::SpriteMode mode, bool flipX, bool flipY, Tyra::Color color);
  void renderRotate(const Sprite& sprite, const Vec2& angle);

 private:
  RendererCore* core;
};

}  // namespace Tyra
