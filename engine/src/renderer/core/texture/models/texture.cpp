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
#include <stdlib.h>
#include <fastmath.h>
#include <string>
#include <sstream>
#include "engine.hpp"
#include "renderer/core/texture/models/texture.hpp"
#include <graph_vram.h>

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

  TYRA_ASSERT(t_data->width <= 512 && t_data->height <= 512,
              "Tyra supports only 512x512 textures max!");

  TYRA_ASSERT(t_data->width == 8 || t_data->width == 16 ||
                  t_data->width == 32 || t_data->width == 64 ||
                  t_data->width == 128 || t_data->width == 256 ||
                  t_data->width == 512,
              "Texture width/height should be 8/16/32/64/128/256/512!");

  TYRA_ASSERT(t_data->height == 8 || t_data->height == 16 ||
                  t_data->height == 32 || t_data->height == 64 ||
                  t_data->height == 128 || t_data->height == 256 ||
                  t_data->height == 512,
              "Texture width/height should be 8/16/32/64/128/256/512!");

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
  return GetVramSize(core->width, core->height, core->psm, GRAPH_ALIGN_BLOCK);
}

u32 Texture::getClutTextureSize() const {
  return GetVramSize(clut->width, clut->height, clut->psm, GRAPH_ALIGN_BLOCK);
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
