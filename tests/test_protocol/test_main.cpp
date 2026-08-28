// Unit tests for components/fiido_bms. Run `pio test -e native` from tests/.
// Fixtures in fixtures.h (sources: live BMS log + reconstructions from parsed values).
#include <unity.h>

#include "test_groups.h"

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();

  run_frame_tests();
  run_polling_tests();
  run_decode_tests();
  run_captured_frame_tests();
  run_state_tests();

  return UNITY_END();
}
