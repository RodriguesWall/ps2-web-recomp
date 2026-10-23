#pragma once

#include <cstdint>

namespace ps2::state {
class Stream;
}

namespace ps2::gif {

void state(ps2::state::Stream &s);

void reset();
void stats();
void transfer(int path, const std::uint8_t *data, std::uint32_t qwc);
void render(int path, const std::uint8_t *data, std::uint32_t qwc);
void render_reset();
void scan_signals(int path, const std::uint8_t *data, std::uint32_t qwc);
bool busy(int path);
std::uint32_t read(std::uint32_t a);
void write(std::uint32_t a, std::uint32_t v);
// MSKPATH3 from VIF1, reported back to the EE in GIF_STAT.M3P
void set_path3_masked(bool masked);
// Channel 2 data. While VIF1 opens PATH3 in MSKPATH3 windows it is held on the side that renders VIF1 (the VU thread
// when there is one) and let through a GIF packet per window, the way a real GIF spreads a texture stream started at
// the end of one frame across the next frame's display list.
void path3_data(const std::uint8_t *data, std::uint32_t qwc);
void path3_receive(const std::uint8_t *data, std::uint32_t qwc);
// VIF1 has just cleared MSKPATH3: let one held packet through
void path3_window();

}
