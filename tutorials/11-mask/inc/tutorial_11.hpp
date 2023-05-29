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

#include <tyra>

namespace Tyra {

class Tutorial11 : public Game {
 public:
  Tutorial11(Engine* engine);
  ~Tutorial11();

  void init();
  void loop();

 private:
  void loadTexture();
  void loadSprite();

  Engine* engine;
  Pad* pad;
  
  Sprite sprite;
  Sprite mask;
};

}  // namespace Tyra
