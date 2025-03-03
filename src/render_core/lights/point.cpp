//
// Created by Zero on 22/10/2022.
//

#include "base/illumination/light.h"
#include "base/mgr/pipeline.h"
#include "base/color/spectrum.h"

namespace vision {

//"type" : "point",
//"param" : {
//    "color" : {
//        "channels" : "xyz",
//        "node" : [1, 1, 1]
//    },
//    "position" : [-0.5, 1.8, 0],
//    "scale" : 0.2
//}
class PointLight : public IPointLight {
private:
    Serial<float3> _position;

public:
    explicit PointLight(const LightDesc &desc)
        : IPointLight(desc),
          _position(desc["position"].as_float3()) {}
    OC_SERIALIZABLE_FUNC(IPointLight, _position)
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    [[nodiscard]] float3 power() const noexcept override {
        return 4 * Pi * average();
    }
    [[nodiscard]] Float3 position() const noexcept override { return *_position; }
    [[nodiscard]] SampledSpectrum Le(const LightSampleContext &p_ref,
                             const LightEvalContext &p_light,
                             const SampledWavelengths &swl) const noexcept override {
        SampledSpectrum value = _color.eval_illumination_spectrum(p_light.uv, swl).sample * scale();
        return value / length_squared(p_ref.pos - position());
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::PointLight)