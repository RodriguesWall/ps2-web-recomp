#include "gif.hpp"

#include "gs.hpp"
#include "queue.hpp"
#include "ps2/system.hpp"

#include "ps2/state.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ps2::gif {

namespace {

struct Path {
    bool active = false;
    std::uint32_t nloop = 0, loop = 0, nreg = 0, reg = 0, flg = 0;
    std::uint64_t regs = 0;
    bool eop = false;
    bool address_data = false;
};

Path g_paths[4];
std::uint32_t g_stat = 0;
std::uint64_t g_tags[4][4];

template <typename Sink>
void walk(Path &p, const std::uint8_t *data, std::uint32_t qwc, Sink &sink) {
    for (std::uint32_t i = 0; i < qwc; ++i) {
        Reg128 q;
        std::memcpy(&q, data + static_cast<std::size_t>(i) * 16, 16);
        if (!p.active) {
            p.nloop = static_cast<std::uint32_t>(q.d[0]) & 0x7FFFu;
            p.eop = (q.d[0] >> 15) & 1u;
            p.flg = static_cast<std::uint32_t>(q.d[0] >> 58) & 3u;
            p.nreg = static_cast<std::uint32_t>(q.d[0] >> 60) & 0xFu;
            if (!p.nreg) p.nreg = 16;
            p.regs = q.d[1];
            p.loop = 0;
            p.reg = 0;
            sink.tag(p, q);
            p.active = p.nloop != 0;
            continue;
        }
        switch (p.flg) {
        case 0:
            sink.packed(static_cast<std::uint32_t>(p.regs >> (p.reg * 4)) & 0xFu, q);
            if (++p.reg >= p.nreg) {
                p.reg = 0;
                if (++p.loop >= p.nloop) p.active = false;
            }
            break;
        case 1:
            for (int half = 0; half < 2 && p.active; ++half) {
                sink.reglist(static_cast<std::uint32_t>(p.regs >> (p.reg * 4)) & 0xFu, q.d[half]);
                if (++p.reg >= p.nreg) {
                    p.reg = 0;
                    if (++p.loop >= p.nloop) p.active = false;
                }
            }
            break;
        default: {
            const std::uint32_t run = std::min(qwc - i, p.nloop - p.loop);
            sink.image(data + static_cast<std::size_t>(i) * 16, run);
            p.loop += run;
            i += run - 1;
            if (p.loop >= p.nloop) p.active = false;
            break;
        }
        }
    }
}

struct GsSink {
    int path;
    void tag(const Path &p, const Reg128 &q) {
        ++g_tags[path][p.flg];
        gs::g_state.q = 1.0f;
        if (((q.d[0] >> 46) & 1u) && p.flg == 0) gs::write(0x00, (q.d[0] >> 47) & 0x7FFu);
    }
    void packed(std::uint32_t reg, const Reg128 &q) { gs::write_packed(reg, q); }
    void reglist(std::uint32_t reg, std::uint64_t value) { gs::write(reg, value); }
    void image(const std::uint8_t *data, std::uint32_t qwords) { gs::write_image(data, qwords); }
};

void scan(Path &p, const std::uint8_t *data, std::uint32_t qwc) {
    std::uint32_t i = 0;
    while (i < qwc) {
        Reg128 q;
        if (!p.active) {
            std::memcpy(&q, data + static_cast<std::size_t>(i) * 16, 16);
            p.nloop = static_cast<std::uint32_t>(q.d[0]) & 0x7FFFu;
            p.flg = static_cast<std::uint32_t>(q.d[0] >> 58) & 3u;
            p.nreg = static_cast<std::uint32_t>(q.d[0] >> 60) & 0xFu;
            if (!p.nreg) p.nreg = 16;
            p.regs = q.d[1];
            p.loop = 0;
            p.reg = 0;
            p.address_data = false;
            if (p.flg == 0)
                for (std::uint32_t r = 0; r < p.nreg; ++r) p.address_data = p.address_data || ((p.regs >> (r * 4)) & 0xFu) == 0xEu;
            p.active = p.nloop != 0;
            ++i;
            continue;
        }
        if (p.flg == 0 && p.address_data) {
            for (; i < qwc && p.active; ++i) {
                const std::uint8_t *at = data + static_cast<std::size_t>(i) * 16;
                if (((p.regs >> (p.reg * 4)) & 0xFu) == 0xEu && at[8] >= 0x50 && at[8] <= 0x62) {
                    std::uint64_t value;
                    std::memcpy(&value, at, 8);
                    if (at[8] >= 0x60) gs::signal(at[8], value);
                    else if (at[8] <= 0x53) gs::note_transfer(at[8], value);
                }
                if (++p.reg >= p.nreg) {
                    p.reg = 0;
                    if (++p.loop >= p.nloop) p.active = false;
                }
            }
            continue;
        }
        const std::uint64_t done = static_cast<std::uint64_t>(p.loop) * p.nreg + p.reg;
        const std::uint64_t left = static_cast<std::uint64_t>(p.nloop) * p.nreg - done;
        std::uint64_t take, used;
        if (p.flg == 0) {
            take = used = std::min<std::uint64_t>(left, qwc - i);
        } else if (p.flg == 1) {
            take = std::min<std::uint64_t>((left + 1) / 2, qwc - i);
            used = std::min<std::uint64_t>(take * 2, left);
        } else {
            take = std::min<std::uint64_t>(p.nloop - p.loop, qwc - i);
            p.loop += static_cast<std::uint32_t>(take);
            if (p.loop >= p.nloop) p.active = false;
            i += static_cast<std::uint32_t>(take);
            continue;
        }
        p.loop = static_cast<std::uint32_t>((done + used) / p.nreg);
        p.reg = static_cast<std::uint32_t>((done + used) % p.nreg);
        if (used == left) p.active = false;
        i += static_cast<std::uint32_t>(take);
    }
}

Path g_scan[4];

// Held channel 2 data, one FIFO per side: [0] the emulation thread, [1] the VU thread that renders VIF1 when it runs.
std::vector<std::uint8_t> g_path3[2];
bool g_path3_masked[2];
// Frame of the last MSKPATH3 window on each side; ~0 for never.
std::uint64_t g_last_window[2] = {~0ull, ~0ull};

int side() { return gs::queue::role() == gs::queue::Role::Vu ? 1 : 0; }

// Bytes of the first complete GIF packet (up to and including the data of the tag with EOP) held for PATH3, or 0.
std::size_t packet_bytes(const std::vector<std::uint8_t> &fifo) {
    const std::size_t qwords = fifo.size() / 16;
    std::size_t at = 0;
    while (at < qwords) {
        std::uint64_t tag;
        std::memcpy(&tag, fifo.data() + at * 16, 8);
        const std::uint64_t nloop = tag & 0x7FFFu, flg = (tag >> 58) & 3u, nreg = ((tag >> 60) & 0xFu) ? (tag >> 60) & 0xFu : 16u;
        at += 1 + (flg == 0 ? nloop * nreg : flg == 1 ? (nloop * nreg + 1) / 2 : nloop);
        if (at > qwords) return 0;
        if ((tag >> 15) & 1u) return at * 16;
    }
    return 0;
}

bool send_packet(int s) {
    std::vector<std::uint8_t> &fifo = g_path3[s];
    const std::size_t bytes = packet_bytes(fifo);
    if (!bytes) return false;
    transfer(3, fifo.data(), static_cast<std::uint32_t>(bytes / 16));
    fifo.erase(fifo.begin(), fifo.begin() + static_cast<std::ptrdiff_t>(bytes));
    return true;
}

void send_all(int s) {
    while (send_packet(s)) {}
}

}

void reset() {
    g_stat = 0;
    for (int s = 0; s < 2; ++s) {
        g_path3[s].clear();
        g_path3_masked[s] = false;
        g_last_window[s] = ~0ull;
    }
    if (gs::queue::active()) {
        for (Path &p : g_scan) p = Path{};
        gs::queue::post(gs::queue::kGifReset, 0, 0, nullptr, 0);
        return;
    }
    render_reset();
}

void render_reset() {
    for (Path &p : g_paths) p = Path{};
}

void render(int path, const std::uint8_t *data, std::uint32_t qwc) {
    GsSink sink{path};
    walk(g_paths[path], data, qwc, sink);
}

void transfer(int path, const std::uint8_t *data, std::uint32_t qwc) {
    if (!gs::queue::active()) {
        render(path, data, qwc);
        return;
    }
    if (gs::queue::role() != gs::queue::Role::Vu) scan(g_scan[path], data, qwc);
    gs::queue::post(gs::queue::kGif, static_cast<std::uint32_t>(path), qwc, data, qwc * 16);
}

void scan_signals(int path, const std::uint8_t *data, std::uint32_t qwc) { scan(g_scan[path], data, qwc); }

bool busy(int path) { return g_paths[path].active; }

void stats() {
    for (int p = 1; p < 4; ++p)
        std::fprintf(stderr, "[gif] path %d tags: packed %llu reglist %llu image %llu\n", p,
                     static_cast<unsigned long long>(g_tags[p][0]), static_cast<unsigned long long>(g_tags[p][1]),
                     static_cast<unsigned long long>(g_tags[p][2] + g_tags[p][3]));
}

std::uint32_t read(std::uint32_t a) {
    // While PATH3 is masked a real GIF holds the stalled channel 2 transfer in its FIFO, and
    // games wait for FQC to read non-zero before they touch the GS; transfers here are
    // immediate, so report the FIFO full for as long as the mask is up.
    if ((a & 0xF0u) == 0x20u) return g_stat | ((g_stat & 2u) ? 16u << 24 : 0u);
    return 0;
}

void write(std::uint32_t a, std::uint32_t v) {
    if ((a & 0xF0u) == 0x00u && (v & 1u)) reset();
    if ((a & 0xF0u) == 0x10u) g_stat = (g_stat & ~1u) | (v & 1u);
}

void set_path3_masked(bool masked) {
    g_path3_masked[side()] = masked;
    g_stat = (g_stat & ~2u) | (masked ? 2u : 0u);
    static const bool trace = std::getenv("PS2_TRACE_PATH3") != nullptr;
    if (trace) std::fprintf(stderr, "[gif] path3 %s\n", masked ? "masked" : "unmasked");
}

void path3_data(const std::uint8_t *data, std::uint32_t qwc) {
    if (gs::queue::vu_active()) {
        // The emulation thread answers SIGNAL/FINISH and downloads now; the VU thread draws the packets in order.
        scan(g_scan[3], data, qwc);
        gs::queue::vu().post(gs::queue::kPath3, qwc, 0, data, qwc * 16);
        return;
    }
    path3_receive(data, qwc);
}

void path3_receive(const std::uint8_t *data, std::uint32_t qwc) {
    const int s = side();
    const std::uint64_t frame = ps2::frame_count();
    if (g_last_window[s] == ~0ull || frame - g_last_window[s] > 2) {
        send_all(s);
        transfer(3, data, qwc);
        return;
    }
    // What the previous display list's windows left behind would have gone out before this new stream on a console.
    if (!g_path3_masked[s]) send_all(s);
    std::vector<std::uint8_t> &fifo = g_path3[s];
    fifo.insert(fifo.end(), data, data + static_cast<std::size_t>(qwc) * 16);
}

void path3_window() {
    const int s = side();
    g_last_window[s] = ps2::frame_count();
    send_packet(s);
}

}

namespace ps2::gif {

void state(ps2::state::Stream &s) {
    s.tag("gif");
    s.pod(g_paths);
    s.pod(g_stat);
    s.pod(g_tags);
    s.pod(g_scan);
}

}
