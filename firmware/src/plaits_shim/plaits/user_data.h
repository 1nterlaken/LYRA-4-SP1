// LYRA-4 — replacement for upstream plaits/user_data.h. NOT an upstream file.
//
// Upstream's version (a) includes <stm32f37x_conf.h> and (b) reads user wavetables and
// 6-op banks from a raw STM32 flash address, 0x08007000. On the nRF52840 that address
// is unmapped: the read would be a bus fault -> k_sys_fatal_error_handler -> reboot,
// the first time any engine was selected.
//
// This shim keeps the same class and interface, and simply has no user data:
//   - ptr() returns NULL, which voice.cc already handles: the 6-op engines fall back to
//     the built-in fm_patches_table, wave terrain to its built-in terrains.
//   - Save() refuses. Nothing in LYRA-4 calls it; the Plaits audio-rate data
//     receiver that did is STM32 firmware we do not use.
//
// It shadows the upstream header because firmware/src/plaits_shim is FIRST on the
// include path (see firmware/CMakeLists.txt). If user data is ever wanted, it belongs
// on the eMMC, not in internal flash -- see docs/SAFETY.md, "Unresolved: the page at 0xFF000".
//
// MIT, like the code it stands in for.

#ifndef PLAITS_USER_DATA_H_
#define PLAITS_USER_DATA_H_

#include <stddef.h>
#include <stdint.h>

namespace plaits {

class UserData {
 public:
  enum {
    ADDRESS = 0,         // unused: there is no user-data region on this device
    SIZE = 0x1000
  };

  UserData() { }
  ~UserData() { }

  inline const uint8_t* ptr(int slot) const {
    (void)slot;
    return NULL;
  }

  inline bool Save(uint8_t* rx_buffer, int slot) {
    (void)rx_buffer;
    (void)slot;
    return false;
  }
};

}  // namespace plaits

#endif  // PLAITS_USER_DATA_H_
