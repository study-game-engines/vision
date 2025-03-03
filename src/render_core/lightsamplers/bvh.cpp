//
// Created by Zero on 28/10/2022.
//

#include "base/illumination/lightsampler.h"
#include "base/mgr/pipeline.h"
#include "base/sampler.h"

namespace vision {

struct LightBVHNode {

};

class BVHLightSampler : public LightSampler {
public:
    explicit BVHLightSampler(const LightSamplerDesc &desc)
        : LightSampler(desc) {}

    void prepare() noexcept override {
        LightSampler::prepare();
        build_bvh();
    }
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    void build_bvh() noexcept {

    }
};
}// namespace vision