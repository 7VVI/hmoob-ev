#pragma once
#include <d3d11.h>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hmmob {

struct AmfPacket {
    std::vector<uint8_t> data;
    int64_t pts_us = 0;
    bool keyframe = false;
};

class AmfEncoder {
public:
    using PacketCallback = std::function<void(const AmfPacket&)>;

    AmfEncoder() = default;
    ~AmfEncoder();
    AmfEncoder(const AmfEncoder&) = delete;
    AmfEncoder& operator=(const AmfEncoder&) = delete;

    bool Initialize(ID3D11Device* device, const std::string& codec,
                    int width, int height, int fps, int bitrate);
    bool Encode(ID3D11Texture2D* nv12_texture, int64_t pts_us, const PacketCallback& callback);
    bool Flush(const PacketCallback& callback);
    void Shutdown();
    bool is_initialized() const;

private:
    struct Impl;
    Impl* impl_ = nullptr;
};

}  // namespace hmmob
