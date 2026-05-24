/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "renderer/2d/renderer_2d.hpp"

namespace Tyra {

Renderer2D::Renderer2D() {}
Renderer2D::~Renderer2D() {}

void Renderer2D::init(RendererCore* t_rendererCore) { core = t_rendererCore; }

void Renderer2D::render(const Sprite* sprite) { render(*sprite); }

void Renderer2D::render(const Sprite& sprite) {
  auto* texture = core->texture.repository.getByTextureId(sprite.textureID);

  TYRA_ASSERT(
      texture, "Texture for sprite with id: ", sprite.id,
      "Was not found in texture repository! Did you forget to add texture?");

  auto texBuffers = core->texture.useTexture(texture);
  core->texture.updateClutBuffer(texBuffers.clut);
  core->renderer2D.render(sprite, texBuffers, texture);
}

void Renderer2D::renderRotate(const Sprite& sprite, const Vec2& angle) {
  auto* texture = core->texture.repository.getByTextureId(sprite.textureID);

  TYRA_ASSERT(
      texture, "Texture for sprite with id: ", sprite.id,
      "Was not found in texture repository! Did you forget to add texture?");

  auto texBuffers = core->texture.useTexture(texture);
  core->texture.updateClutBuffer(texBuffers.clut);
  core->renderer2D.renderRotate(sprite, texBuffers, texture, angle);
}

void Renderer2D::renderSprite(u32 textureID, float x, float y, Tyra::Vec2 offset, Tyra::Vec2 size, float scale, float rotation, Tyra::SpriteMode mode, bool flipX, bool flipY, Tyra::Color color){
  auto* texture = core->texture.repository.getByTextureId(textureID);

  TYRA_ASSERT(
      texture, "Texture for render sprite ",
      "Was not found in texture repository! Did you forget to add texture?");

  auto texBuffers = core->texture.useTexture(texture);
  core->texture.updateClutBuffer(texBuffers.clut);
  if(rotation == 0){
    core->renderer2D.renderSpriteRotate(Vec2{x,y}, offset, size, scale, rotation, mode, flipX, flipY, color, texBuffers, texture);
  }else{
    core->renderer2D.renderSprite(Vec2{x,y}, offset, size, scale, mode, flipX, flipY, color, texBuffers, texture);
  }
}

}  // namespace Tyra
