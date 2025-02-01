/*
# _____        ____   ___
#   |     \/   ____| |___|
#   |     |   |   \  |   |
#-----------------------------------------------------------------------
# Copyright 2022-2022, tyra - https://github.com/h4570/tyrav2
# Licensed under Apache License 2.0
# Sandro Sobczyński <sandro.sobczynski@gmail.com>
# Wellington Carvalho <wellcoj@gmail.com>
*/

#include <kernel.h>
#include <libpad.h>

#pragma once

namespace Tyra {

struct PadButtons {
  u8 Cross, Square, Triangle, Circle, DpadUp, DpadDown, DpadLeft, DpadRight, L1,
      L2, L3, R1, R2, R3, Start, Select;
};

struct PadJoy {
  u8 h, v, isCentered, isMoved;
};

/** Class responsible for player pad */
class Pad {
 public:
  Pad();
  ~Pad();

  void init();
  void update();

  inline const PadButtons& getClicked() const { return clicked; }
  inline const PadButtons& getPressed() const { return pressed; }
  inline const PadJoy& getLeftJoyPad() const { return leftJoyPad; }
  inline const PadJoy& getRightJoyPad() const { return rightJoyPad; }

 private:
  char* bufferData[256] alignas(sizeof(char) * 64);
  char actAlign[6];
  int actuators, ret, port, slot; // 4 * 4 = 16
  padButtonStatus buttons;     // 32
  u32 padData, oldPad, newPad; //  4 * 3 = 12
  PadButtons pressed, clicked; // 16 * 2 = 32
  PadJoy leftJoyPad, rightJoyPad;

  void reset();
  void handleClickedButtons();
  void handlePressedButtons();
  int waitPadReady();
  int initPad();
};

}  // namespace Tyra