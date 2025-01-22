/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
*/

#include "engine.hpp"
#include "thread/threading.hpp"
#include "renderer/core/paths/path1/path1.hpp"
#include "info/version.hpp"
#include <iostream>
#include <cstring>
#include <iomanip>
#include "info/banner_data.cpp"

#include <dma.h>
#include <tamtypes.h>
#include <draw.h>
#include <graph.h>
#include <gs_privileged.h>
#include <gs_psm.h>
#include <packet2_utils.h>

namespace Tyra {

static Audio audio;
static Pad pad;
static Info info;
static IrxLoader irx;
static Path1 path1;
static path3Lib path3;
static EngineCoreData core;
static EngineRendererCoreGS rendererGS;
static RendererCoreTextureSenderLib sender;
static EngineRendererCoreTexture engineCoreTexture;
static RendererCore3DLib engineCore3D;
static EngineRendererCore2D engineCore2D;
static RendererCoreSyncLib engineCoreSync;
static Color bgColor;
static bool isFrameLimitOn;

void EngineCoreData::print() const {
  auto text = getPrint();
  printf("%s\n", text.c_str());
}

std::string EngineCoreData::getPrint() const {
  std::stringstream res;
  res << "RendererSettings(";
  res << "width: " << width << ", ";
  res << "height: " << height << ", ";
  res << "near: " << near << ", ";
  res << "far: " << far << ", ";
  res << "projectionScale: " << projectionScale << ", ";
  res << "aspectRatio: " << aspectRatio << ", ";
  res << "interlaced height: " << interlacedHeightF;
  res << ")";
  return res.str();
}

EngineCoreData getSettings() { return core; }

EngineRendererCoreGS::EngineRendererCoreGS() {
  context = 0;
  currentField = 0;
}

EngineRendererCoreGS::~EngineRendererCoreGS() {
  if (flipPacket) {
    packet2_free(flipPacket);
  }
  if (zTestPacket) {
    packet2_free(zTestPacket);
  }
}

path3Lib::path3Lib() {
  drawFinishPacket = packet2_create(3, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
  clearScreenPacket = packet2_create(36, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
  texturePacket = packet2_create(128, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);

  packet2_chain_open_end(drawFinishPacket, 0, 0);
  packet2_update(drawFinishPacket, draw_finish(drawFinishPacket->next));
  packet2_chain_close_tag(drawFinishPacket);
}

path3Lib::~path3Lib() {
  packet2_free(drawFinishPacket);
  packet2_free(clearScreenPacket);
  packet2_free(texturePacket);
}

void path3Lib::init() {
  dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);

  TYRA_LOG("Path3 initialized");
}

void path3Lib::sendDrawFinishTag() {
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(drawFinishPacket, DMA_CHANNEL_GIF, true);
}

void path3Lib::clearScreen(zbuffer_t* z, const Color& color) {
  packet2_reset(clearScreenPacket, false);
  packet2_chain_open_end(clearScreenPacket, 0, 0);
  packet2_update(clearScreenPacket,
                 draw_disable_tests(clearScreenPacket->next, 0, z));
  packet2_update(
      clearScreenPacket,
      draw_clear(clearScreenPacket->next, 0, 2048.0F - (core.width / 2),
                 2048.0F - (core.height / 2), core.width, core.height,
                 static_cast<int>(color.r), static_cast<int>(color.g),
                 static_cast<int>(color.b)));
  packet2_update(clearScreenPacket,
                 draw_enable_tests(clearScreenPacket->next, 0, z));
  packet2_update(clearScreenPacket, draw_finish(clearScreenPacket->next));
  packet2_chain_close_tag(clearScreenPacket);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(clearScreenPacket, DMA_CHANNEL_GIF, true);
}

void path3Lib::sendTexture(const Texture* texture,
                           const RendererCoreTextureBuffers& texBuffers) {
  packet2_reset(texturePacket, false);

  packet2_update(
      texturePacket,
      draw_texture_transfer(texturePacket->base, texture->core->data,
                            texture->getWidth(), texture->getHeight(),
                            texture->core->psm, texBuffers.core->address,
                            texBuffers.core->width));

  if (texBuffers.clut != nullptr) {
    auto* clut = texture->clut;
    packet2_update(
        texturePacket,
        draw_texture_transfer(texturePacket->next, clut->data, clut->width,
                              clut->height, clut->psm, texBuffers.clut->address,
                              texBuffers.clut->width));
  }

  packet2_chain_open_cnt(texturePacket, 0, 0, 0);
  packet2_update(texturePacket,
                 draw_texture_wrapping(
                     texturePacket->next, 0,
                     const_cast<texwrap_t*>(texture->getWrapSettings())));
  packet2_chain_close_tag(texturePacket);

  packet2_update(texturePacket, draw_texture_flush(texturePacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(texturePacket, DMA_CHANNEL_GIF, true);
}

RendererCoreTextureSenderLib::RendererCoreTextureSenderLib() {}
RendererCoreTextureSenderLib::~RendererCoreTextureSenderLib() {}

void RendererCoreTextureSenderLib::init() {
  TYRA_LOG("Renderer texture initialized!");
}

RendererCoreTextureBuffers RendererCoreTextureSenderLib::allocate(
    const Texture* t_texture) {
  texbuffer_t* core = allocateTextureCore(t_texture);
  texbuffer_t* clut = nullptr;

  auto texClut = t_texture->clut;
  if (texClut != nullptr && texClut->width > 0) {
    clut = allocateTextureClut(t_texture);
  }
  return {t_texture->id, core, clut};
}

float RendererCoreTextureSenderLib::getSizeInMB(texbuffer_t* texBuffer) {
  auto bpp = getBppByPsm(texBuffer->psm);
  auto width = pow(2, texBuffer->info.width);
  auto height = pow(2, texBuffer->info.height);
  return (width / 100.0F) * (height / 100.0F) * (bpp / 100.0F) / 8.0F;
}

void RendererCoreTextureSenderLib::deallocate(
    const RendererCoreTextureBuffers& texBuffers) {
  if (texBuffers.clut != nullptr && texBuffers.clut->width > 0) {
    rendererGS.vram.free(texBuffers.clut->address);
    delete texBuffers.clut;
  }

  rendererGS.vram.free(texBuffers.core->address);

  delete texBuffers.core;
}

texbuffer_t* RendererCoreTextureSenderLib::allocateTextureCore(
    const Texture* t_texture) {
  auto* result = new texbuffer_t;
  const auto* core = t_texture->core;

  switch (core->psm) {
    case GS_PSM_8:
    case GS_PSM_4:
    case GS_PSM_8H:
    case GS_PSM_4HL:
    case GS_PSM_4HH:
      result->width = -128 & (core->width + 127);
      break;
    default:
      result->width = -64 & (core->width + 63);
      break;
  }

  result->psm = core->psm;
  result->info.components = core->components;

  auto address = rendererGS.vram.allocate(*core);
  TYRA_ASSERT(address > 0, "Texture buffer allocation error, no memory!");
  result->address = address;

  result->info.width = draw_log2(t_texture->getWidth());
  result->info.height = draw_log2(t_texture->getHeight());
  result->info.function = TEXTURE_FUNCTION_MODULATE;
  return result;
}

texbuffer_t* RendererCoreTextureSenderLib::allocateTextureClut(
    const Texture* t_texture) {
  auto* result = new texbuffer_t;
  const auto* clut = t_texture->clut;

  int clutWidth = clut->width <= 64 ? 64 : clut->width;

  result->width = clutWidth;
  result->psm = clut->psm;
  result->info.components = clut->components;

  auto address = rendererGS.vram.allocate(*clut);
  TYRA_ASSERT(address > 0, "Texture clut buffer allocation error, no memory!");
  result->address = address;

  result->info.width = draw_log2(clut->width);
  result->info.height = draw_log2(clut->height);
  result->info.function = TEXTURE_FUNCTION_MODULATE;
  return result;
}

TextureBpp RendererCoreTextureSenderLib::getBppByPsm(const u32& psm) {
  if (psm == GS_PSM_32) {
    return bpp32;
  } else if (psm == GS_PSM_24) {
    return bpp24;
  } else if (psm == GS_PSM_8) {
    return bpp8;
  } else if (psm == GS_PSM_4) {
    return bpp4;
  } else {
    TYRA_TRAP("Unknown bpp!");
    return bpp32;
  }
}

void EngineRendererCoreTexture::init() {
  sender.init();
  repository.init(&currentAllocations);
  initClut();
}

void EngineRendererCoreTexture::updateClutBuffer(texbuffer_t* clutBuffer) {
  if (clutBuffer == nullptr || clutBuffer->width == 0) {
    clut.psm = 0;
    clut.load_method = CLUT_NO_LOAD;
    clut.address = 0;
  } else {
    clut.psm = clutBuffer->psm;
    clut.load_method = CLUT_LOAD;
    clut.address = clutBuffer->address;
  }
}

RendererCoreTextureBuffers EngineRendererCoreTexture::useTexture(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  auto allocated = getAllocatedBuffersByTextureId(t_tex->id);
  if (allocated.id != 0) return allocated;

  if (rendererGS.vram.getSizeInMB(*t_tex) >=
      rendererGS.vram.getFreeSpaceInMB()) {
    for (int i = currentAllocations.size() - 1; i >= 0; i--) {
      sender.deallocate(currentAllocations[i]);
    }
    currentAllocations.clear();
  }

  auto newTexBuffer = sender.allocate(t_tex);
  path3.sendTexture(t_tex, newTexBuffer);
  registerAllocation(newTexBuffer);

  return newTexBuffer;
}

RendererCoreTextureBuffers EngineRendererCoreTexture::updateTextureInfo(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  auto allocated = getAllocatedBuffersByTextureId(t_tex->id);
  TYRA_ASSERT(allocated.id != 0, "Can't update an unallocated texture!");

  path3.sendTexture(t_tex, allocated);
  return allocated;
}

RendererCoreTextureBuffers
EngineRendererCoreTexture::getAllocatedBuffersByTextureId(const u32& t_id) {
  for (u32 i = 0; i < currentAllocations.size(); i++)
    if (currentAllocations[i].id == t_id) return currentAllocations[i];
  return {0, nullptr, nullptr};
}

void EngineRendererCoreTexture::registerAllocation(
    const RendererCoreTextureBuffers& t_buffers) {
  currentAllocations.push_back(t_buffers);
}

void EngineRendererCoreTexture::unregisterAllocation(const u32& textureId) {
  u32 foundIndex;

  for (u32 i = 0; i < currentAllocations.size(); i++) {
    if (currentAllocations[i].id == textureId) {
      foundIndex = i;
      break;
    }
  }

  currentAllocations.erase(currentAllocations.begin() + foundIndex);
}

void EngineRendererCoreTexture::initClut() {
  clut.storage_mode = CLUT_STORAGE_MODE1;
  clut.start = 0;
  clut.psm = 0;
  clut.load_method = CLUT_NO_LOAD;
  clut.address = 0;
  TYRA_LOG("Clut set!");
}

TextureRepository& getTextureRepository() {
  return engineCoreTexture.repository;
}

EngineRenderer3DFrustumPlanes::EngineRenderer3DFrustumPlanes() {
  lastFov = 0.0F;
}
EngineRenderer3DFrustumPlanes::~EngineRenderer3DFrustumPlanes() {}

void EngineRenderer3DFrustumPlanes::init(const float& fov) {
  computeStaticData(fov);

  TYRA_LOG("Frustum planes initialized!");
}

void EngineRenderer3DFrustumPlanes::update(const CameraInfo3D& cameraInfo,
                                           const float& fov) {
  computeStaticData(fov);

  // compute the Z axis of camera
  Z = *cameraInfo.position - *cameraInfo.looksAt;
  Z.normalize();

  // X axis of camera of given "up" vector and Z axis
  X = cameraInfo.up->cross(Z);
  X.normalize();

  // the real "up" vector is the cross product of Z and X
  Y = Z.cross(X);

  // compute the center of the near and far planes
  nearCenter = *cameraInfo.position - Z * core.near;
  farCenter = *cameraInfo.position - Z * core.far;

  // compute the 8 corners of the frustum
  ntl = nearCenter + Y * nearHeight - X * nearWidth;
  ntr = nearCenter + Y * nearHeight + X * nearWidth;
  nbl = nearCenter - Y * nearHeight - X * nearWidth;
  nbr = nearCenter - Y * nearHeight + X * nearWidth;

  ftl = farCenter + Y * farHeight - X * farWidth;
  fbr = farCenter - Y * farHeight + X * farWidth;
  ftr = farCenter + Y * farHeight + X * farWidth;
  fbl = farCenter - Y * farHeight - X * farWidth;

  frustumPlanes[0].update(ntr, ntl, ftl);  // Top
  frustumPlanes[1].update(nbl, nbr, fbr);  // BOTTOM
  frustumPlanes[2].update(ntl, nbl, fbl);  // LEFT
  frustumPlanes[3].update(nbr, ntr, fbr);  // RIGHT
  frustumPlanes[4].update(ntl, ntr, nbr);  // NEAR
  frustumPlanes[5].update(ftr, ftl, fbl);  // FAR
}

void EngineRenderer3DFrustumPlanes::computeStaticData(const float& fov) {
  if (fabs(fov - lastFov) < 0.00001F) return;

  lastFov = fov;
  float tang = tanf(fov * Math::HALF_ANG2RAD);
  nearHeight = tang * core.near;
  nearWidth = nearHeight * core.aspectRatio;
  farHeight = tang * core.far;
  farWidth = farHeight * core.aspectRatio;
}

void EngineRenderer3DFrustumPlanes::print() const {
  auto text = getPrint(nullptr);
  printf("%s\n", text.c_str());
}

void EngineRenderer3DFrustumPlanes::print(const char* name) const {
  auto text = getPrint(name);
  printf("%s\n", text.c_str());
}

std::string EngineRenderer3DFrustumPlanes::getPrint(const char* name) const {
  std::stringstream res;
  if (name) {
    res << name << "(";
  } else {
    res << "EngineRenderer3DFrustumPlanes(";
  }
  res << std::fixed << std::setprecision(2);
  res << std::endl;

  res << frustumPlanes[0].getPrint("Top") << std::endl;
  res << frustumPlanes[1].getPrint("Bottom") << std::endl;
  res << frustumPlanes[2].getPrint("Left") << std::endl;
  res << frustumPlanes[3].getPrint("Right") << std::endl;
  res << frustumPlanes[4].getPrint("Near") << std::endl;
  res << frustumPlanes[5].getPrint("Far") << ")";

  return res.str();
}

RendererCore3DLib::RendererCore3DLib() {
  fov = 60.0F;
  is3DSupportEnabled = false;
}
RendererCore3DLib::~RendererCore3DLib() {}

void RendererCore3DLib::update() { is3DSupportEnabled = false; }

void RendererCore3DLib::update(const CameraInfo3D& cameraInfo) {
  frustumPlanes.update(cameraInfo, fov);
  M4x4::lookAt(&view, *cameraInfo.position, *cameraInfo.looksAt);
  viewProj = projection * view;
  is3DSupportEnabled = true;
}

void RendererCore3DLib::init() {
  frustumPlanes.init(fov);
  setProjection();
  TYRA_LOG("RendererCore3DLib initialized!");
}

const M4x4& RendererCore3DLib::getView() {
  TYRA_ASSERT(is3DSupportEnabled,
              "You can't compute 3D without camera information. Please correct "
              "your beginFrame()");
  return view;
}

const M4x4& RendererCore3DLib::getViewProj() {
  TYRA_ASSERT(is3DSupportEnabled,
              "You can't compute 3D without camera information. Please correct "
              "your beginFrame()");
  return viewProj;
}

void RendererCore3DLib::setFov(const float& t_fov) {
  fov = t_fov;
  setProjection();
}

void RendererCore3DLib::setProjection() {
  projection =
      M4x4::perspective(fov, core.width, core.height, core.projectionScale,
                        core.aspectRatio, core.near, core.far);
}

u32 RendererCore3DLib::uploadVU1Program(VU1Program* program,
                                        const u32& address) {
  return path1.uploadProgram(program, address);
}

void RendererCore3DLib::setVU1DoubleBuffers(const u16& startingAddress,
                                            const u16& bufferSize) {
  path1.setDoubleBuffer(startingAddress, bufferSize);
}

EngineRendererCore2D::EngineRendererCore2D() {
  context = 0;
  packets[0] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packets[1] = packet2_create(16, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  rects[0] = new texrect_t;
  rects[1] = new texrect_t;

  setPrim();
  setLod();
}

EngineRendererCore2D::~EngineRendererCore2D() {
  packet2_free(packets[0]);
  packet2_free(packets[1]);
  delete rects[0];
  delete rects[1];
}

const float EngineRendererCore2D::GS_DRAW_AREA = 4096.0F;
const float EngineRendererCore2D::SCREEN_CENTER = 4096.0F / 2.0F;

void EngineRendererCore2D::setPrim() {
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;
}

void EngineRendererCore2D::setLod() {
  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_LINEAR;
  lod.min_filter = LOD_MIN_LINEAR;
  lod.mipmap_select = LOD_MIPMAP_REGISTER;
  lod.l = 0;
  lod.k = 0.0F;
}

void EngineRendererCore2D::init() {}

void EngineRendererCore2D::render(const Sprite& sprite,
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
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core,
                                    &engineCoreTexture.clut);
  draw_enable_blending();
  packet2_update(packet, draw_rect_textured(packet->next, 0, rect));

  packet2_update(packet,
                 draw_primitive_xyoffset(packet->next, 0,
                                         SCREEN_CENTER - (core.width / 2.0F),
                                         SCREEN_CENTER - (core.height / 2.0F)));
  draw_disable_blending();
  packet2_update(packet, draw_finish(packet->next));

  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(packet, DMA_CHANNEL_GIF, true);

  context = !context;
}

void EngineRendererCore2D::setTextureMappingType(
    const PipelineTextureMappingType textureMappingType) {
  lod.mag_filter = textureMappingType;
  lod.min_filter = textureMappingType;
}

RendererCoreSyncLib::RendererCoreSyncLib() {}
RendererCoreSyncLib::~RendererCoreSyncLib() {}

void RendererCoreSyncLib::init() {
  // path3 = t_path3;
  // path1 = t_path1;
}

void RendererCoreSyncLib::align3D() {
  clear();
  sendPath1Req();
  waitAndClear();
}

void RendererCoreSyncLib::align2D() {
  clear();
  sendPath3Req();
  waitAndClear();
}

void RendererCoreSyncLib::sendPath1Req() { path1.sendDrawFinishTag(); }

void RendererCoreSyncLib::sendPath3Req() { path3.sendDrawFinishTag(); }

void RendererCoreSyncLib::addPath1Req(packet2_t* packet) {
  path1.addDrawFinishTag(packet);
}

u8 RendererCoreSyncLib::check() { return *GS_REG_CSR & 2; }

void RendererCoreSyncLib::clear() { *GS_REG_CSR |= 2; }

void RendererCoreSyncLib::waitAndClear() {
  while (!check()) {
  }
  clear();
}

void initChannels() { dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0); }

void allocateBuffers() {
  rendererGS.frameBuffers[0].width = static_cast<unsigned int>(core.width);
  rendererGS.frameBuffers[0].height = static_cast<unsigned int>(core.height);
  rendererGS.frameBuffers[0].mask = 0;
  rendererGS.frameBuffers[0].psm = GS_PSM_32;
  rendererGS.frameBuffers[0].address = rendererGS.vram.allocateBuffer(
      rendererGS.frameBuffers[0].width, rendererGS.frameBuffers[0].height,
      rendererGS.frameBuffers[0].psm);

  rendererGS.frameBuffers[1].width = rendererGS.frameBuffers[0].width;
  rendererGS.frameBuffers[1].height = rendererGS.frameBuffers[0].height;
  rendererGS.frameBuffers[1].mask = rendererGS.frameBuffers[0].mask;
  rendererGS.frameBuffers[1].psm = rendererGS.frameBuffers[0].psm;
  rendererGS.frameBuffers[1].address = rendererGS.vram.allocateBuffer(
      rendererGS.frameBuffers[1].width, rendererGS.frameBuffers[1].height,
      rendererGS.frameBuffers[1].psm);

  rendererGS.zBuffer.enable = DRAW_ENABLE;
  rendererGS.zBuffer.mask = 0;
  rendererGS.zBuffer.method = ZTEST_METHOD_GREATER_EQUAL;
  rendererGS.zBuffer.zsm = GS_ZBUF_32;
  rendererGS.zBuffer.address = rendererGS.vram.allocateBuffer(
      rendererGS.frameBuffers[0].width, rendererGS.frameBuffers[0].height,
      rendererGS.zBuffer.zsm);

  graph_initialize(
      rendererGS.frameBuffers[1].address, rendererGS.frameBuffers[1].width,
      rendererGS.frameBuffers[1].height, rendererGS.frameBuffers[1].psm, 0, 0);

  // Interlacing tests
  // graph_set_mode(GRAPH_MODE_INTERLACED, GRAPH_MODE_NTSC, GRAPH_MODE_FRAME,
  //                GRAPH_ENABLE);
  // graph_set_screen(0, 0, static_cast<int>(core.width),
  //                  static_cast<int>(core.height));
  // graph_set_bgcolor(0, 0, 0);
  // graph_set_framebuffer_filtered(frameBuffers[1].address,
  // frameBuffers[1].width,
  //                                frameBuffers[1].psm, 0, 0);
  // graph_enable_output();

  TYRA_LOG("Framebuffers, zBuffer set and allocated!");
}

void enableZTests() {
  packet2_reset(rendererGS.zTestPacket, false);
  packet2_update(
      rendererGS.zTestPacket,
      draw_enable_tests(rendererGS.zTestPacket->base, 0, &rendererGS.zBuffer));
  packet2_update(rendererGS.zTestPacket,
                 draw_finish(rendererGS.zTestPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(rendererGS.zTestPacket, DMA_CHANNEL_GIF, true);
}

void initDrawingEnvironment() {
  packet2_t* packet2 = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packet2_update(
      packet2, draw_setup_environment(packet2->base, 0, rendererGS.frameBuffers,
                                      &rendererGS.zBuffer));
  packet2_update(packet2, draw_primitive_xyoffset(
                              packet2->next, 0,
                              rendererGS.screenCenter - (core.width / 2.0F),
                              rendererGS.screenCenter - (core.height / 2.0F)));
  packet2_update(packet2, draw_finish(packet2->next));
  dma_channel_send_packet2(packet2, DMA_CHANNEL_GIF, true);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  packet2_free(packet2);
  TYRA_LOG("Drawing environment initialized!");
}

qword_t* setXYOffset(qword_t* q, const int& drawContext, const float& x,
                     const float& y) {
  PACK_GIFTAG(q, GIF_SET_TAG(1, 0, 0, 0, GIF_FLG_PACKED, 1), GIF_REG_AD);
  q++;

  int yOffset = rendererGS.currentField == GRAPH_FIELD_ODD ? 8 : 0;

  PACK_GIFTAG(q,
              GS_SET_XYOFFSET(static_cast<int>(x * 16.0F),
                              static_cast<int>((y * 16.0F + yOffset))),
              GS_REG_XYOFFSET + drawContext);
  q++;

  return q;
}

void flipBuffers() {
  graph_set_framebuffer_filtered(
      rendererGS.frameBuffers[rendererGS.context].address,
      rendererGS.frameBuffers[rendererGS.context].width,
      rendererGS.frameBuffers[rendererGS.context].psm, 0, 0);

  rendererGS.context ^= 1;

  packet2_update(
      rendererGS.flipPacket,
      draw_framebuffer(rendererGS.flipPacket->base, 0,
                       &rendererGS.frameBuffers[rendererGS.context]));
  // Interlacing test
  // packet2_update(
  //     flipPacket,
  //     setXYOffset(flipPacket->next, 0,
  //                 screenCenter - (core.width / 2.0F),
  //                 screenCenter - (core.interlacedHeightF / 2.0F)));

  packet2_update(rendererGS.flipPacket,
                 draw_finish(rendererGS.flipPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(rendererGS.flipPacket, DMA_CHANNEL_GIF, true);
  draw_wait_finish();

  // Interlacing test
  // updateCurrentField();
}

void updateCurrentField() {
  if (*GS_REG_CSR & (1 << 13)) {
    rendererGS.currentField = GRAPH_FIELD_ODD;
    return;
  }

  rendererGS.currentField = GRAPH_FIELD_EVEN;
}

void initCoreGS() {
  initChannels();
  rendererGS.flipPacket =
      packet2_create(4, P2_TYPE_UNCACHED_ACCL, P2_MODE_NORMAL, 0);
  rendererGS.zTestPacket = packet2_create(8, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  allocateBuffers();
  initDrawingEnvironment();

  TYRA_LOG("Renderer core initialized!");
}

void beginFrame() {
  engineCore3D.update();
  Threading::switchThread();
  path3.clearScreen(&rendererGS.zBuffer, bgColor);
}

void endFrame() {
  Threading::switchThread();
  if (isFrameLimitOn) graph_wait_vsync();
  flipBuffers();
}

void setClearScreenColor(const Color& color) { bgColor = color; }

void render(const Sprite& sprite) {
  auto* texture = engineCoreTexture.repository.getBySpriteId(sprite.id);

  TYRA_ASSERT(
      texture, "Texture for sprite with id: ", sprite.id,
      "Was not found in texture repository! Did you forget to add texture?");

  auto texBuffers = engineCoreTexture.useTexture(texture);
  engineCoreTexture.updateClutBuffer(texBuffers.clut);
  engineCore2D.render(sprite, texBuffers, texture);
}

void showBanner() {
  auto* bannerData = ___createTyraSplashBanner();

  TextureBuilderData tbd;
  tbd.bpp = bpp32;
  tbd.gsComponents = TEXTURE_COMPONENTS_RGBA;
  tbd.width = 128;
  tbd.height = 32;
  tbd.clut = nullptr;
  tbd.data = reinterpret_cast<unsigned char*>(bannerData);

  Sprite sprite;
  sprite.size.x = 128;
  sprite.size.y = 32;
  sprite.position.x = (core.width / 2) - (sprite.size.x / 2);
  sprite.position.y = (core.height / 2) - (sprite.size.y / 2);

  auto texture = Texture(&tbd);
  texture.addLink(sprite.id);
  engineCoreTexture.repository.add(&texture);

  for (int i = 0; i < 2; i++) {
    beginFrame();
    render(sprite);
    endFrame();
  }

  engineCoreTexture.repository.removeById(texture.id);
  texture.core->data = nullptr;
  delete[] bannerData;

  std::cout << "\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "        _____        ____   ___\n";
  std::cout << "          |     \\/   ____| |___|\n";
  std::cout << "          |     |   |   \\  |   |\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "Copyright 2022\n";
  std::cout << "Repository: https://github.com/h4570/tyra\n";
  std::cout << "Licensed under Apache License 2.0\n";
  std::cout << "Version: ";
  std::cout << Version::toString().c_str();
  std::cout << "\n";
  std::cout << "-----------------------------------------\n";
  std::cout << "\n";
}

void InitEngine(const EngineOptions& options) {
  info.writeLogsToFile = options.writeLogsToFile;
  srand(time(nullptr));
  irx.loadAll(options.loadUsbDriver, info.writeLogsToFile);
  // renderer.init();
  path3.init();
  // engineCoreSync.init(); // no hace nada
  initCoreGS();
  engineCoreTexture.init();
  engineCore3D.init();
  engineCore2D.init();
  showBanner();
  audio.init();
  pad.init();
}

void BeginDrawing(void) {}

Engine::Engine() { initAll(false); }

Engine::Engine(const EngineOptions& options) {
  // info.writeLogsToFile = options.writeLogsToFile;
  // initAll(options.loadUsbDriver);
}

Engine::~Engine() {}

void Engine::run(Game* t_game) {
  game = t_game;
  game->init();
  while (true) {
    realLoop();
  }
}

void Engine::realLoop() {
  // pad.update();
  // game->loop();
  // info.update();
}

void Engine::initAll(const bool& loadUsbDriver) {
  // srand(time(nullptr));
  // irx.loadAll(loadUsbDriver, info.writeLogsToFile);
  // renderer.init();
  // banner.show(&renderer);
  // audio.init();
  // pad.init();
}

}  // namespace Tyra
