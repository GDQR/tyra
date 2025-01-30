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
#include <graph_vram.h>

namespace Tyra {

static Audio audio;
static Pad pad;
static Info info;
static IrxLoader irx;
static Path1 path1;
static EngineCoreData core;
static EngineRendererCoreGS rendererGS;
static RendererCoreTextureSenderLib sender;
static EngineRendererCoreTexture engineCoreTexture;
static RendererCore3DLib engineCore3D;
static EngineRendererCore2D engineCore2D;
static Color bgColor;
static bool isFrameLimitOn = true;

static zbuffer_t zBuffer;
static RendererCoreGSVRam vram;

static packet2_t* zTestPacket;

static packet2_t* clearScreenPacketPath3;
static packet2_t* drawFinishPacketPath3;
static packet2_t* texturePacketPath3;

static packet2_t* flipPacket;

static const float GS_DRAW_AREA = 4096.0F;
static const float SCREEN_CENTER = 4096.0F / 2.0F;
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

Info getInfo() { return info; }

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

void initPath3() {
  drawFinishPacketPath3 =
      packet2_create(3, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
  clearScreenPacketPath3 =
      packet2_create(36, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);
  texturePacketPath3 =
      packet2_create(128, P2_TYPE_NORMAL, P2_MODE_CHAIN, false);

  packet2_chain_open_end(drawFinishPacketPath3, 0, 0);
  packet2_update(drawFinishPacketPath3,
                 draw_finish(drawFinishPacketPath3->next));
  packet2_chain_close_tag(drawFinishPacketPath3);
  dma_channel_initialize(DMA_CHANNEL_GIF, nullptr, 0);

  TYRA_LOG("Path3 initialized");
}

void sendDrawFinishTagPath3() {
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(drawFinishPacketPath3, DMA_CHANNEL_GIF, true);
}

void clearScreenPath3(zbuffer_t* z, const Color& color) {
  packet2_reset(clearScreenPacketPath3, false);
  packet2_chain_open_end(clearScreenPacketPath3, 0, 0);
  packet2_update(clearScreenPacketPath3,
                 draw_disable_tests(clearScreenPacketPath3->next, 0, z));
  packet2_update(
      clearScreenPacketPath3,
      draw_clear(clearScreenPacketPath3->next, 0, 2048.0F - (core.width / 2),
                 2048.0F - (core.height / 2), core.width, core.height,
                 static_cast<int>(color.r), static_cast<int>(color.g),
                 static_cast<int>(color.b), static_cast<int>(color.a)));
  packet2_update(clearScreenPacketPath3,
                 draw_enable_tests(clearScreenPacketPath3->next, 0, z));
  packet2_update(clearScreenPacketPath3,
                 draw_finish(clearScreenPacketPath3->next));
  packet2_chain_close_tag(clearScreenPacketPath3);
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(clearScreenPacketPath3, DMA_CHANNEL_GIF, true);
}

void sendTextureWithPath3(const Texture* texture,
                          const RendererCoreTextureBuffers& texBuffers) {
  packet2_reset(texturePacketPath3, false);

  packet2_update(
      texturePacketPath3,
      draw_texture_transfer(texturePacketPath3->base, texture->core->data,
                            texture->getWidth(), texture->getHeight(),
                            texture->core->psm, texBuffers.core->address,
                            texBuffers.core->width));

  if (texBuffers.clut != nullptr) {
    auto* clut = texture->clut;
    packet2_update(
        texturePacketPath3,
        draw_texture_transfer(texturePacketPath3->next, clut->data, clut->width,
                              clut->height, clut->psm, texBuffers.clut->address,
                              texBuffers.clut->width));
  }

  packet2_chain_open_cnt(texturePacketPath3, 0, 0, 0);
  packet2_update(texturePacketPath3,
                 draw_texture_wrapping(
                     texturePacketPath3->next, 0,
                     const_cast<texwrap_t*>(texture->getWrapSettings())));
  packet2_chain_close_tag(texturePacketPath3);

  packet2_update(texturePacketPath3,
                 draw_texture_flush(texturePacketPath3->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(texturePacketPath3, DMA_CHANNEL_GIF, true);
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
    vram.free(texBuffers.clut->address);
    delete texBuffers.clut;
  }

  vram.free(texBuffers.core->address);

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

  auto address = vram.allocate(*core);
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

  result->width = 64;
  result->psm = clut->psm;
  result->info.components = clut->components;

  auto address = vram.allocate(*clut);
  TYRA_ASSERT(address > 0, "Texture clut buffer allocation error, no memory!");
  result->address = address;

  result->info.width = draw_log2(clut->width);
  result->info.height = draw_log2(clut->height);
  result->info.function = TEXTURE_FUNCTION_MODULATE;
  return result;
}

TextureBpp getBppByPsm(const u32& psm) {
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

static clutbuffer_t clut;

void initClut() {
  clut.storage_mode = CLUT_STORAGE_MODE1;
  clut.start = 0;
  clut.psm = 0;
  clut.load_method = CLUT_NO_LOAD;
  clut.address = 0;
  TYRA_LOG("Clut set!");
}

static std::vector<RendererCoreTextureBuffers> currentAllocations;
void EngineRendererCoreTexture::init() {
  sender.init();
  repository.init(&currentAllocations);
  initClut();
}

void updateClutBuffer(texbuffer_t* clutBuffer) {
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

RendererCoreTextureBuffers getAllocatedBuffersByTextureId(const u32& t_id) {
  for (u32 i = 0; i < currentAllocations.size(); i++)
    if (currentAllocations[i].id == t_id) return currentAllocations[i];
  return {0, nullptr, nullptr};
}

void registerAllocation(const RendererCoreTextureBuffers& t_buffers) {
  currentAllocations.push_back(t_buffers);
}

void unregisterAllocation(const u32& textureId) {
  u32 foundIndex;

  for (u32 i = 0; i < currentAllocations.size(); i++) {
    if (currentAllocations[i].id == textureId) {
      foundIndex = i;
      break;
    }
  }

  currentAllocations.erase(currentAllocations.begin() + foundIndex);
}

RendererCoreTextureBuffers EngineRendererCoreTexture::useTexture(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  auto allocated = getAllocatedBuffersByTextureId(t_tex->id);
  if (allocated.id != 0) return allocated;

  if (vram.getSizeInMB(*t_tex) >= vram.getFreeSpaceInMB()) {
    for (int i = currentAllocations.size() - 1; i >= 0; i--) {
      sender.deallocate(currentAllocations[i]);
    }
    currentAllocations.clear();
  }

  auto newTexBuffer = sender.allocate(t_tex);
  sendTextureWithPath3(t_tex, newTexBuffer);
  registerAllocation(newTexBuffer);

  return newTexBuffer;
}

RendererCoreTextureBuffers EngineRendererCoreTexture::updateTextureInfo(
    const Texture* t_tex) {
  TYRA_ASSERT(t_tex != nullptr, "Provided nullptr texture!");

  auto allocated = getAllocatedBuffersByTextureId(t_tex->id);
  TYRA_ASSERT(allocated.id != 0, "Can't update an unallocated texture!");

  sendTextureWithPath3(t_tex, allocated);
  return allocated;
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

static bool is3DSupportEnabled;
/** Current camera frustum planes. */
static EngineRenderer3DFrustumPlanes frustumPlanes;
static float fov;
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

const float& RendererCore3DLib::getFov() { return fov; }

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

static prim_t prim;
static lod_t lod;

void setPrim() {
  prim.type = PRIM_TRIANGLE;
  prim.shading = PRIM_SHADE_GOURAUD;
  prim.mapping = DRAW_ENABLE;
  prim.fogging = DRAW_DISABLE;
  prim.blending = DRAW_ENABLE;
  prim.antialiasing = DRAW_DISABLE;
  prim.mapping_type = PRIM_MAP_ST;
  prim.colorfix = PRIM_UNFIXED;
}

void setLod() {
  lod.calculation = LOD_USE_K;
  lod.max_level = 0;
  lod.mag_filter = LOD_MAG_LINEAR;
  lod.min_filter = LOD_MIN_LINEAR;
  lod.mipmap_select = LOD_MIPMAP_REGISTER;
  lod.l = 0;
  lod.k = 0.0F;
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
  packet2_utils_gs_add_texbuff_clut(packet, texBuffers.core, &clut);
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

void setTextureMappingType(
    const PipelineTextureMappingType textureMappingType) {
  lod.mag_filter = textureMappingType;
  lod.min_filter = textureMappingType;
}

void align3D() {
  clear();
  path1.sendDrawFinishTag();
  waitAndClear();
}

void align2D() {
  clear();
  sendDrawFinishTagPath3();
  waitAndClear();
}
void addPath1Req(packet2_t* packet) { path1.addDrawFinishTag(packet); }

u8 check() { return *GS_REG_CSR & 2; }

void clear() { *GS_REG_CSR |= 2; }

void waitAndClear() {
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
  rendererGS.frameBuffers[0].address = vram.allocateBuffer(
      rendererGS.frameBuffers[0].width, rendererGS.frameBuffers[0].height,
      rendererGS.frameBuffers[0].psm);

  rendererGS.frameBuffers[1].width = rendererGS.frameBuffers[0].width;
  rendererGS.frameBuffers[1].height = rendererGS.frameBuffers[0].height;
  rendererGS.frameBuffers[1].mask = rendererGS.frameBuffers[0].mask;
  rendererGS.frameBuffers[1].psm = rendererGS.frameBuffers[0].psm;
  rendererGS.frameBuffers[1].address = vram.allocateBuffer(
      rendererGS.frameBuffers[1].width, rendererGS.frameBuffers[1].height,
      rendererGS.frameBuffers[1].psm);

  zBuffer.enable = DRAW_ENABLE;
  zBuffer.mask = 0;
  zBuffer.method = ZTEST_METHOD_GREATER_EQUAL;
  zBuffer.zsm = GS_ZBUF_32;
  zBuffer.address =
      vram.allocateBuffer(rendererGS.frameBuffers[0].width,
                          rendererGS.frameBuffers[0].height, zBuffer.zsm);

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
  packet2_reset(zTestPacket, false);
  packet2_update(zTestPacket,
                 draw_enable_tests(zTestPacket->base, 0, &zBuffer));
  packet2_update(zTestPacket, draw_finish(zTestPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(zTestPacket, DMA_CHANNEL_GIF, true);
}

void initDrawingEnvironment() {
  packet2_t* packet2 = packet2_create(20, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  packet2_update(
      packet2, draw_setup_environment(packet2->base, 0, rendererGS.frameBuffers,
                                      &zBuffer));
  packet2_update(packet2,
                 draw_primitive_xyoffset(packet2->next, 0,
                                         SCREEN_CENTER - (core.width / 2.0F),
                                         SCREEN_CENTER - (core.height / 2.0F)));
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

  packet2_update(flipPacket, draw_framebuffer(
                                 flipPacket->base, 0,
                                 &rendererGS.frameBuffers[rendererGS.context]));
  // Interlacing test
  // packet2_update(
  //     flipPacket,
  //     setXYOffset(flipPacket->next, 0,
  //                 screenCenter - (core.width / 2.0F),
  //                 screenCenter - (core.interlacedHeightF / 2.0F)));

  packet2_update(flipPacket, draw_finish(flipPacket->next));
  dma_channel_wait(DMA_CHANNEL_GIF, 0);
  dma_channel_send_packet2(flipPacket, DMA_CHANNEL_GIF, true);
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
  flipPacket = packet2_create(4, P2_TYPE_UNCACHED_ACCL, P2_MODE_NORMAL, 0);
  zTestPacket = packet2_create(8, P2_TYPE_NORMAL, P2_MODE_NORMAL, 0);
  allocateBuffers();
  initDrawingEnvironment();

  TYRA_LOG("Renderer core initialized!");
}

void beginFrame() {
  engineCore3D.update();
  Threading::switchThread();
  clearScreenPath3(&zBuffer, bgColor);
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
  updateClutBuffer(texBuffers.clut);
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
  initPath3();
  initCoreGS();
  engineCoreTexture.init();
  engineCore3D.init();
  showBanner();
  audio.init();
  pad.init();
}

void BeginDrawing(void) {}

int GetVramSize(int width, int height, const int psm, const int alignment) {
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

  if (psm == GS_PSM_4 || psm == GS_PSM_16) {
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
  } else if (psm == GS_PSM_8 || psm == GS_PSM_24 || psm == GS_PSM_32 ||
             psm == GS_PSM_8H || psm == GS_PSM_4HL || psm == GS_PSM_4HH ||
             psm == GS_PSMZ_24 || psm == GS_PSMZ_32) {
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
  } else if (psm == GS_PSMZ_16) {
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
  } else if (psm == GS_PSM_16S) {
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
  } else if (psm == GS_PSMZ_16S) {
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
     * carryWidth * carryHeight = size block
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
     * 2 = 1*2 + 1 = 3 block
     * 3 = 2*2     = 4 block
     **/
    if (carryWidth == 1 && carryHeight == 2) {
      size += 1;
    }
  }

  size += carryWidth * carryHeight;

  size *= GRAPH_ALIGN_BLOCK;

  size = -alignment & (size + (alignment - 1));

  return size;
}

void Engine::run(Game* t_game) {
  game = t_game;
  game->init();
  while (true) {
    realLoop();
  }
}

void Engine::realLoop() {
  pad.update();
  game->loop();
  info.update();
}

}  // namespace Tyra
