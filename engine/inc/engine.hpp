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

#include "./renderer/renderer.hpp"
#include "./pad/pad.hpp"
#include "./audio/audio.hpp"
#include "./irx/irx_loader.hpp"
#include "./info/info.hpp"
#include "./info/banner.hpp"
#include "./game.hpp"

namespace Tyra {

struct EngineOptions {
  /**
   * True -> logs will be written to file.
   * False -> logs will be displayed in console
   */
  bool writeLogsToFile = false;

  bool loadUsbDriver = false;
};

class Engine {
 public:
  void run(Game* t_game);

 private:
  // IrxLoader irx;

  Game* game;
  Banner banner;

  void realLoop();
};
class EngineCoreData {
 public:
  EngineCoreData()
      : width(512.0F),
        height(448.0F),
        interlacedHeightF(height / 2),
        near(0.1F),
        far(51200.0F),
        projectionScale(4096.0F),
        aspectRatio(width / height),
        interlacedHeightUI(static_cast<unsigned int>(interlacedHeightF)) {}

  void print() const;
  std::string getPrint() const;

  float width;
  float height;
  float interlacedHeightF;
  float near;
  float far;
  float projectionScale;
  float aspectRatio;
  unsigned int interlacedHeightUI;
};

class EngineRendererCoreGS {
 public:
  EngineRendererCoreGS();
  ~EngineRendererCoreGS();

  zbuffer_t zBuffer;
  RendererCoreGSVRam vram;

  // void flipBuffers();

  // void enableZTests();

  constexpr static float gsCenter = 4096.0F;
  constexpr static float screenCenter = gsCenter / 2.0F;

  framebuffer_t frameBuffers[2];
  packet2_t* flipPacket;
  packet2_t* zTestPacket;
  u8 context;
  u8 currentField;

  // void allocateBuffers();
  // void initDrawingEnvironment();
  // void initChannels();
  // void updateCurrentField();
  // qword_t* setXYOffset(qword_t* q, const int& drawContext, const float& x,
  //                      const float& y);
};

class RendererCoreTextureSenderLib {
 public:
  RendererCoreTextureSenderLib();
  ~RendererCoreTextureSenderLib();

  void init();

  RendererCoreTextureBuffers allocate(const Texture* t_texture);

  void deallocate(const RendererCoreTextureBuffers& texBuffers);

  float getSizeInMB(texbuffer_t* texBuffer);

 private:
  texbuffer_t* allocateTextureCore(const Texture* t_texture);
  texbuffer_t* allocateTextureClut(const Texture* t_texture);
};

class EngineRendererCoreTexture {
 public:
  clutbuffer_t clut;
  TextureRepository repository;

  RendererCoreTextureBuffers useTexture(const Texture* t_tex);

  /**
   * Called by user after changing texture wrap settings
   * Updates texture packet without reallocate it
   */
  RendererCoreTextureBuffers updateTextureInfo(const Texture* t_tex);

  /** Called by renderer during initialization */
  void init();

  /** Called by renderer during rendering */
  void updateClutBuffer(texbuffer_t* clutBuffer);

 private:
  std::vector<RendererCoreTextureBuffers> currentAllocations;

  void initClut();
  void registerAllocation(const RendererCoreTextureBuffers& t_buffers);
  void unregisterAllocation(const u32& textureId);
  RendererCoreTextureBuffers getAllocatedBuffersByTextureId(const u32& id);
};

class EngineRenderer3DFrustumPlanes {
 public:
  EngineRenderer3DFrustumPlanes();
  ~EngineRenderer3DFrustumPlanes();

  void init(const float& fov);
  void update(const CameraInfo3D& cameraInfo, const float& fov);
  const Plane& get(u8 index) const { return frustumPlanes[index]; }
  const Plane* getAll() const { return frustumPlanes; }
  const Plane& operator[](u8 index) const { return frustumPlanes[index]; }

  void print() const;
  void print(const char* name) const;
  void print(const std::string& name) const { print(name.c_str()); }
  std::string getPrint(const char* name = nullptr) const;

 private:
  void computeStaticData(const float& fov);
  float lastFov;
  Plane frustumPlanes[6];
  float nearHeight, nearWidth, farHeight, farWidth;
  Vec4 nearCenter, farCenter, X, Y, Z, ntl, ntr, nbl, nbr, ftl, fbr, ftr, fbl;
};

class RendererCore3DLib {
 public:
  RendererCore3DLib();
  ~RendererCore3DLib();

  /** Current camera frustum planes. */
  EngineRenderer3DFrustumPlanes frustumPlanes;

  /** Called by renderer. */
  void init();

  const float& getFov() const { return fov; }

  void setFov(const float& t_fov);

  /**
   * Called by beginFrame();
   * Sets 3D support to off
   */
  void update();

  /**
   * Called by beginFrame();
   * Updates camera info, to get proper frustum culling.
   * Sets 3D support to on
   */
  void update(const CameraInfo3D& cameraInfo);

  /** Get projection (screen) matrix */
  const M4x4& getProjection() { return projection; }

  /**
   * Get view (camera) matrix
   * Updated at every beginFrame()
   */
  const M4x4& getView();

  /**
   * Get projection * view matrix
   * Updated at every beginFrame()
   */
  const M4x4& getViewProj();

  /**
   * @brief
   * Upload VU1 program.
   * Please pay attention that you will be REPLACING
   * current VU1 programs
   *
   * @param address Starting address of your program.
   * @return Address of the end of your program, so next program can start from
   * this + 1
   */
  u32 uploadVU1Program(VU1Program* program, const u32& address);

  /**
   * @brief Set VU1 double buffer
   *
   * @param startingAddress Starting address. Example 10.
   * @param bufferSize Buffer size. Example 490, so second buffer will start
   * from 490+10
   */
  void setVU1DoubleBuffers(const u16& startingAddress, const u16& bufferSize);

 private:
  M4x4 view, projection, viewProj;
  float fov;
  bool is3DSupportEnabled;

  void setProjection();
};

class EngineRendererCore2D {
 public:
  EngineRendererCore2D();
  ~EngineRendererCore2D();

  void init();

  void render(const Sprite& sprite,
              const RendererCoreTextureBuffers& texBuffers, Texture* texture);

  void setTextureMappingType(
      const PipelineTextureMappingType textureMappingType);

 private:
  void setPrim();
  void setLod();

  prim_t prim;
  lod_t lod;

  static const float GS_DRAW_AREA;
  static const float SCREEN_CENTER;

  u8 context;
  packet2_t* packets[2];
  texrect_t* rects[2];
};

void InitEngine(const EngineOptions& options);

void BeginDrawing(void);
void beginFrame();
void endFrame();
void render(const Sprite& sprite);

TextureBpp getBppByPsm(const u32& psm);
int GetVramSize(int width, int height, const int psm, const int alignment);

/** Clear the screen based on the screen's origin, width, and height using the
 * defined color. **/
qword_t* draw_clear(qword_t* q, int context, float x, float y, float width,
                    float height, int r, int g, int b, int a);
Info getInfo();
EngineCoreData getSettings();
TextureRepository& getTextureRepository();

void setClearScreenColor(const Color& color);

// Path3
void clearScreenPath3(zbuffer_t* z, const Color& color);
void sendDrawFinishTagPath3();  
void sendTextureWithPath3(const Texture* texture,
                   const RendererCoreTextureBuffers& texBuffers);
// sync

/**
 * Synchronization class.
 * Mainly between VU1 and EE.
 *
 * For example you can set texture, render X vertices, then add() wait, and
 * wait() for it. Without it, there is risk for example to send new texture
 * during drawing with previous one.
 */

// --- Auto

/** clear() -> sendPath1Req() -> waitAndClear() */
void align3D();

/** clear() -> sendPath3Req() -> waitAndClear() */
void align2D();

// --- Manual

u8 check();
void clear();
void waitAndClear();
void addPath1Req(packet2_t* packet);

}  // namespace Tyra
