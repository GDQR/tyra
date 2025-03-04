/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2024, tyra - https://github.com/h4570/tyra
# Licensed under Apache License 2.0
# Guido Diego Quispe Robles
*/

#include <loaders/texture/png_loader.hpp>
#include <loaders/texture/builder/texture_builder_data.hpp>
#include <renderer/core/texture/models/texture.hpp>
#include <malloc.h>
#include "font/font.hpp"

namespace Tyra {

namespace TyraFont {
FT_Library library;
std::vector<u32> deletedIDs;
std::vector<FT_Byte*> dataFromFontData;
unsigned char* clutData;
RendererCoreTexture* rendererTexture;
Renderer2D* renderer2D;
std::vector<int> codepoints;
Sprite sprite;
int offsetX;
int offsetY;
unsigned int previous;
FT_UInt glyphindex;
FT_Vector delta;
bool hasKerning;
u32 MAXID = 256;
};  // namespace TyraFont

FontData::FontData() { dataID = TyraFont::MAXID; }

Font::~Font() {
  delete TyraFont::clutData;
  for (unsigned int i = 0; TyraFont::dataFromFontData.size(); i++) {
    free(TyraFont::dataFromFontData[i]);
  }
  TyraFont::dataFromFontData.clear();
  TyraFont::deletedIDs.clear();
  TyraFont::codepoints.clear();
}

void Font::init(Renderer* t_renderer) {
  TyraFont::renderer2D = &t_renderer->renderer2D;
  TyraFont::rendererTexture = &t_renderer->core.texture;

  TyraFont::sprite.scale = 1.0f;
  TyraFont::sprite.size = Vec2(256, 256);

  FT_Error error = FT_Init_FreeType(&TyraFont::library);
  if (error) {
    TYRA_ASSERT(!(true == true),
                "Error occurred during library initialization");
  }

  TyraFont::clutData =
      new (std::align_val_t(128)) unsigned char[1024]();  // 16 * 16 * 4
  int j = 0;
  for (int i = 0; i < 256; i++) {
    TyraFont::clutData[j++] = 255;
    TyraFont::clutData[j++] = 255;
    TyraFont::clutData[j++] = 255;
    TyraFont::clutData[j++] = (i * 128) / 255;
  }
  TYRA_LOG("Font manager initialized!");
}

void Font::loadFontFromMemory(FontData* font, const char* filePath,
                              int fontSize, int* codePoints,
                              const int codePointCount) {
  // Set font data from memory
  FILE* file = fopen(filePath, "rb");
  fseek(file, 0, SEEK_END);
  size_t size = ftell(file);
  fseek(file, 0, SEEK_SET);

  FT_Byte* data = reinterpret_cast<FT_Byte*>(malloc(size));

  fread(data, 1, size, file);

  fclose(file);

  FT_Face face;
  FT_Error error = FT_New_Memory_Face(TyraFont::library, data, size, 0, &face);

  TYRA_ASSERT(
      error != FT_Err_Unknown_File_Format,
      "The font file could be opened and read, but it appears, that its font "
      "format is unsupported");

  TyraFont::dataFromFontData.push_back(data);
  font->face = face;

  error = FT_Set_Pixel_Sizes(face, 0, fontSize);

  if (error != 0) {
    TYRA_ASSERT(!(true == true), "error setting size");
  }

  if (TyraFont::deletedIDs.empty() == false) {
    font->dataID = TyraFont::deletedIDs.front();
    TyraFont::deletedIDs.erase(TyraFont::deletedIDs.begin());
  } else {
    font->dataID = TyraFont::dataFromFontData.size() - 1;
  }

  TextureAtlas textureAtlas;

  TextureBuilderData textureData;

  if (FT_HAS_COLOR(face) == true) {
    TYRA_LOG("Font has color");
    textureData.width = 256;
    textureData.height = 256;
    textureData.data =
        new (std::align_val_t(128)) unsigned char[256 * 256 * 4]();
    textureData.bpp = bpp32;
  } else {
    TYRA_LOG("Font has no color");
    textureData.width = 256;
    textureData.height = 256;
    textureData.data = new (std::align_val_t(128)) unsigned char[256 * 256]();
    textureData.bpp = bpp8;
    textureData.clutWidth = 16;
    textureData.clutHeight = 16;
    textureData.clut = TyraFont::clutData;
    textureData.clutBpp = bpp32;
  }

  Texture* texture = new Texture(&textureData);
  font->textureID = texture->id;
  TyraFont::rendererTexture->repository.add(texture);
  TyraFont::rendererTexture->useTexture(texture);

  font->textureAtlas = textureAtlas;

  if (codePoints == NULL) {
    for (unsigned int i = 32; i < 126; i++) {
      createGlyphTexture(font, fontSize, i);
    }
  } else {
    for (int i = 0; i < codePointCount; i++) {
      createGlyphTexture(font, fontSize, codePoints[i] /*, fontSize*/);
    }
  }
  TYRA_LOG("Font Loaded!");
}

void Font::loadFont(FontData* font, int fontSize, const char* filePath) {
  loadFontFromMemory(font, filePath, fontSize, NULL, 95);
}

void Font::unloadGlyphs(FontData* font) {
  TYRA_LOG("Unloading Glyphs");
  font->glyph.clear();
  font->glyphID.clear();
}

void Font::unloadFontDataRAM(FontData* font) {
  if (font->dataID != TyraFont::MAXID) {
    unloadGlyphs(font);
    for(unsigned int i = 0; i<TyraFont::dataFromFontData.size();i++){
      if(i == font->dataID){
        free(TyraFont::dataFromFontData[font->dataID]);
        break;
      }
    }
    TyraFont::dataFromFontData.erase(TyraFont::dataFromFontData.begin() +
                                     font->dataID);
    TyraFont::deletedIDs.push_back(font->dataID);
    font->dataID = TyraFont::MAXID;
  }
}

void Font::unloadFontDataVRAM(FontData* font) {
  if (font->dataID != TyraFont::MAXID) {
    FT_Done_Face(font->face);
    unloadFontDataRAM(font);
    if (TyraFont::rendererTexture->repository.getIndexOf(font->textureID) !=
        -1) {
      TyraFont::rendererTexture->repository.free(font->textureID);
      font->textureID =
          TyraFont::rendererTexture->repository.getTexturesCount();
    }
    TYRA_LOG("Unloaded Font data from RAM and VRAM successful");
  }
}

bool Font::getGlyphIndex(FontData* font, unsigned int* glyph,
                         const int fontSize, const int codepoint) {
  for (unsigned int i = 0; i < font->glyphID.size(); i++) {
    if (fontSize == font->glyphID[i].fontSize &&
        codepoint == font->glyphID[i].glyphCodepoint) {
      *glyph = i;
      return true;
    }
  }
  return false;
}

int Font::getCodepoint(const char* text, unsigned int* bytes) {
  /* UTF-8 more info in https://en.wikipedia.org/wiki/UTF-8
    Code point ↔ UTF-8 conversion
    First code and last code point are in hexadecimal.
    If use 2 bytes or more, from the byte 2 they need to be 10xxxxxx until
    the last byte used. The "x" means can be 1 or 0.
    |----------------------------------------------------------------------|
    |First code point |Last code point |Byte 1  |Byte 2  |Byte 3  |Byte 4  |
    |(0000-0000)      |(0000-007F)     |0xxxxxxx|nothing |nothing |nothing |
    |(0000-0080)      |(0000-07FF)     |110xxxxx|10xxxxxx|nothing |nothing |
    |(0000-0800)      |(0000-FFFF)     |1110xxxx|10xxxxxx|10xxxxxx|nothing |
    |(0001-0000)      |(0010-FFFF)     |11110xxx|10xxxxxx|10xxxxxx|10xxxxxx|
    |----------------------------------------------------------------------|
    0x80 = 1000-0000
    0xC0 = 1100-0000
    0xE0 = 1110-0000
    0xF0 = 1111-0000
    0xF8 = 1111-1000
  */

  int codepoint = 0x3f;  // char '?'
  *bytes = 1;

  // 0x00 minimum value allowed, 0x80 minimum value NOT allowed
  if (0x00 == (0x80 & text[0])) {
    codepoint = text[0];
  }  // 0xC0 minimum value allowed, 0xE0 minimum value NOT allowed
  else if (0xC0 == (0xE0 & text[0])) {
    if ((text[1] & 0xC0) == 0x80) {  // verify that it is 10xxxxxx.
      codepoint = ((text[0] ^ 0xC0) << 6) | ((text[1] ^ 0x80));
      *bytes = 2;
    }
  }  // 0xE0 minimum value allowed, 0xF0 minimum value NOT allowed
  else if (0xE0 == (0xF0 & text[0])) {
    if ((text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80) {
      codepoint = ((text[0] ^ 0xE0) << 12) | ((text[1] ^ 0x80) << 6) |
                  ((text[2] ^ 0x80));
      *bytes = 3;
    }
  }  // 0xF0 minimum value allowed, 0xF8 minimum value NOT allowed
  else if (0xF0 == (0xF8 & text[0])) {
    if ((text[1] & 0xC0) == 0x80 && (text[2] & 0xC0) == 0x80 &&
        (text[3] & 0xC0) == 0x80) {
      codepoint = ((text[0] ^ 0xF0) << 18) | ((text[1] ^ 0x80) << 12) |
                  ((text[2] ^ 0x80) << 6) | ((text[3] ^ 0x80));
      *bytes = 4;
    }
  }

  return codepoint;
}

int getSizeTexture(const int size) {
  if (size <= 8) {
    return 8;
  } else if (size <= 16) {
    return 16;
  } else if (size <= 32) {
    return 32;
  } else if (size <= 64) {
    return 64;
  } else if (size <= 128) {
    return 128;
  } else if (size <= 256) {
    return 256;
  }
  return 512;
}

void Font::createGlyphTexture(FontData* font, const int fontSize,
                              const int codepoint) {
  FT_Error error = FT_Set_Pixel_Sizes(font->face, 0, fontSize);

  if (error != 0) {
    TYRA_ASSERT(!(true == true), "error setting size");
  }

  FT_GlyphSlot slot = font->face->glyph;

  int flags = FT_LOAD_DEFAULT;
  if (FT_HAS_COLOR(font->face) == true) {
    flags |= FT_LOAD_COLOR;
  }

  FT_UInt glyph_index = FT_Get_Char_Index(font->face, codepoint);

  error = FT_Load_Glyph(font->face, glyph_index, flags);

  if (error) {
    TYRA_ASSERT(!(true == true), "Error loading glyph", error);
  }

  error = FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL);
  if (error) {
    TYRA_ASSERT(!(true == true), "Error render glyph", error);
  }

  int width = 256;

  Glyph newglyph;
  newglyph.size = Vec2(slot->bitmap.width, slot->bitmap.rows);
  newglyph.advanceX = slot->advance.x / 64;
  newglyph.bearing = Vec2(slot->bitmap_left, slot->bitmap_top);

  GlyphID newGlyphID;
  newGlyphID.fontSize = fontSize;
  newGlyphID.glyphCodepoint = codepoint;

  Texture* texture =
      TyraFont::rendererTexture->repository.getByTextureId(font->textureID);

  TYRA_ASSERT(texture != nullptr, "Texture Not Found");

  int colorSize = 1;
  if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
    colorSize = 4;
  }
  int pospixel;

  if (slot->bitmap.width + font->textureAtlas.x < 256 &&
      slot->bitmap.rows + font->textureAtlas.maxHeight < 256) {
    pospixel = (256 * colorSize * font->textureAtlas.y) + font->textureAtlas.x;
    newglyph.posAtlas.x = font->textureAtlas.x;
    font->textureAtlas.x += slot->bitmap.width;

    if (font->textureAtlas.y + slot->bitmap.rows >
        font->textureAtlas.maxHeight) {
      font->textureAtlas.maxHeight = font->textureAtlas.y + slot->bitmap.rows;
    }
  } else if (slot->bitmap.rows + font->textureAtlas.maxHeight < 256) {
    newglyph.posAtlas.x = 0;
    font->textureAtlas.x = slot->bitmap.width;
    font->textureAtlas.y = font->textureAtlas.maxHeight;
    font->textureAtlas.maxHeight += slot->bitmap.rows;
    pospixel = 256 * colorSize * font->textureAtlas.y;
  } else {
    // Clear all glyphs and texture data
    pospixel = 0;
    unloadGlyphs(font);
    newglyph.posAtlas.x = 0;
    font->textureAtlas.x = slot->bitmap.width;
    font->textureAtlas.y = 0;
    font->textureAtlas.maxHeight = slot->bitmap.rows;
    memset(texture->core->data, 0, 65536);  // 256*256 = 65536
  }

  newglyph.posAtlas.y = font->textureAtlas.y;

  font->glyph.push_back(newglyph);
  font->glyphID.push_back(newGlyphID);

  unsigned char* bitmapPixels = slot->bitmap.buffer;

  if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_GRAY) {
    for (unsigned int i = 0; i < slot->bitmap.rows; i++) {
      for (unsigned int j = 0; j < slot->bitmap.width; j++) {
        texture->core->data[pospixel++] = *bitmapPixels++;
      }

      pospixel += width - slot->bitmap.width;
    }
  } else if (slot->bitmap.pixel_mode == FT_PIXEL_MODE_BGRA) {
    struct PngPixel4* pixels =
        reinterpret_cast<PngPixel4*>(texture->core->data);
    for (unsigned int i = 0; i < slot->bitmap.rows; i++) {
      for (unsigned int j = 0; j < slot->bitmap.width; j++) {
        pixels[pospixel].b = *bitmapPixels++;
        pixels[pospixel].g = *bitmapPixels++;
        pixels[pospixel].r = *bitmapPixels++;
        pixels[pospixel++].a = (int)(*bitmapPixels++ * 128 / 255);
      }

      pospixel += width - slot->bitmap.width;
    }
  }

  if (TyraFont::rendererTexture->getAllocatedBuffersByTextureId(texture->id)
          .id != 0) {
    TyraFont::rendererTexture->updateTextureInfo(texture);
  }
}

void Font::drawText(FontData* font, const char* text, float x, float y,
                    int fontSize, Color color) {
  TyraFont::offsetX = 0;
  TyraFont::offsetY = 0;
  TyraFont::previous = 0;
  /*
    The color of the texture is not changed by pixel
    Texture function is modulate.
    So the texture final color is gonna be
    (texture red   pixel * color red by user)   >> 7
    (texture blue  pixel * color blue by user)  >> 7
    (texture green pixel * color green by user) >> 7
    (texture alpha pixel * color alpha by user) >> 7
    Sometimes it will have the approximate color
  */

  color.r = color.r * 128 / 255;
  color.g = color.g * 128 / 255;
  color.b = color.b * 128 / 255;

  TyraFont::sprite.color = color;

  unsigned int textLenght = 0;

  while (text[textLenght] != '\0') {
    textLenght++;
  }

  unsigned int indexGlyph = 0;
  for (unsigned int i = 0; i < textLenght;) {
    TyraFont::codepoints.push_back(getCodepoint(&text[i], &indexGlyph));
    i += indexGlyph;
  }

  textLenght = TyraFont::codepoints.size();

  TyraFont::rendererTexture->repository.getByTextureId(font->textureID)
      ->addLink(TyraFont::sprite.id);

  TyraFont::hasKerning = FT_HAS_KERNING(font->face);

  for (unsigned int i = 0; i < textLenght; i++) {
    if (TyraFont::codepoints[i] == '\n') {
      TyraFont::offsetY += fontSize;
      TyraFont::offsetX = 0.0f;
    } else if (TyraFont::codepoints[i] != '\t') {
      if (getGlyphIndex(font, &indexGlyph, fontSize, TyraFont::codepoints[i]) ==
          false) {
        createGlyphTexture(font, fontSize, TyraFont::codepoints[i]);

        indexGlyph = font->glyphID.size() - 1;
      }

      if (TyraFont::hasKerning == true) {
        TyraFont::glyphindex =
            FT_Get_Char_Index(font->face, TyraFont::codepoints[i]);
        if (TyraFont::previous && TyraFont::glyphindex) {
          FT_Get_Kerning(font->face, TyraFont::previous, TyraFont::glyphindex,
                         FT_KERNING_DEFAULT, &TyraFont::delta);

          TyraFont::offsetX += TyraFont::delta.x / 64;
        }
        TyraFont::previous = TyraFont::glyphindex;
      }

      TyraFont::sprite.position.x = x + static_cast<float>(TyraFont::offsetX) +
                                    font->glyph[indexGlyph].bearing.x;

      TyraFont::sprite.position.y = y + static_cast<float>(TyraFont::offsetY) -
                                    font->glyph[indexGlyph].bearing.y;

      TyraFont::sprite.size = font->glyph[indexGlyph].size;
      TyraFont::sprite.offset = font->glyph[indexGlyph].posAtlas;

      TyraFont::offsetX += font->glyph[indexGlyph].advanceX;

      TyraFont::renderer2D->render(TyraFont::sprite);
    } else if (TyraFont::codepoints[i] == '\t') {
      TyraFont::offsetX += (8 * fontSize / 32) * 2;
    }
  }
  TyraFont::codepoints.clear();
  TyraFont::rendererTexture->repository.getByTextureId(font->textureID)
      ->removeLinkById(TyraFont::sprite.id);
}

bool Font::fontIsValid(u32 fontID, u32 textureID) {
  if (fontID < TyraFont::MAXID) {
    if (TyraFont::rendererTexture->repository.getByTextureId(textureID) !=
        nullptr) {
      return true;
    }
  }
  return false;
}

void Font::setMaxIDFont(u32 maxID) {
  if (maxID >= 256) TyraFont::MAXID = maxID;
}
}  // namespace Tyra