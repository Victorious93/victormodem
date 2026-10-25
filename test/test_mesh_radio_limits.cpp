// Host test: g++ -std=c++17 -Iinclude -DMESH_BOARD_RAK4631 test/test_mesh_radio_limits.cpp && ./a.out
#include <cassert>
#include <cstdio>
#include "mesh_radio_limits.h"
int main() {
  assert(mesh_channel_hz(0) == 903000000UL && mesh_channel_hz(7) == 914200000UL);
  assert(mesh_channel_hz(8) == 0);
  assert(mesh_tx_allowed(4, 7, 500000, 222));
  assert(mesh_tx_allowed(4, 9, 500000, 222));
  assert(!mesh_tx_allowed(4, 7, 250000, 100));   // BW250 rejected
  assert(!mesh_tx_allowed(4, 10, 500000, 32));   // SF10 rejected
  assert(!mesh_tx_allowed(4, 7, 500000, 223));
  assert(mesh_clamp_tx_dbm(22) == 15);
  printf("SF7/222B=%u us SF9/222B=%u us\n", mesh_airtime_us(7, 222), mesh_airtime_us(9, 222));
  return 0;
}
