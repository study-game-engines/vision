//
// Created by Zero on 29/11/2022.
//

#include "base/scattering/medium.h"
#include "base/sampler.h"
#include "base/mgr/pipeline.h"

namespace vision {

class HomogeneousMedium : public Medium {
private:
    float3 _sigma_a;
    float3 _sigma_s;
    float3 _sigma_t;
    float _g;

public:
    explicit HomogeneousMedium(const MediumDesc &desc)
        : Medium(desc),
          _sigma_a(desc.sigma_a["value"].as_float3() * _scale),
          _sigma_s(desc.sigma_s["value"].as_float3() * _scale),
          _sigma_t(_sigma_a + _sigma_s),
          _g(desc.g["value"].as_float()) {}
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    [[nodiscard]] SampledSpectrum Tr(Float t, const SampledWavelengths &swl) const noexcept {
        SampledSpectrum sigma_t = spectrum().decode_to_unbound_spectrum(_sigma_t, swl).sample;
        return exp(-sigma_t * min(RayTMax, t));
    }

    [[nodiscard]] SampledSpectrum Tr(const OCRay &ray, const SampledWavelengths &swl,
                             Sampler *sampler) const noexcept override {
        return Tr(length(ray->direction()) * ray->t_max(), swl);
    }

    [[nodiscard]] SampledSpectrum sample(const OCRay &ray, Interaction &it,
                                 const SampledWavelengths &swl,
                                 Sampler *sampler) const noexcept override {
        SampledSpectrum sigma_t = spectrum().decode_to_unbound_spectrum(_sigma_t, swl).sample;
        SampledSpectrum sigma_s = spectrum().decode_to_unbound_spectrum(_sigma_s, swl).sample;
        Uint channel = min(cast<uint>(sampler->next_1d() * swl.dimension()), swl.dimension() - 1);
        Float dist = -log(1 - sampler->next_1d()) / sigma_t[channel];
        Float t = min(dist / length(ray->direction()), ray->t_max());
        Bool sampled_medium = t < ray->t_max();
        $if(sampled_medium) {
            it = Interaction(ray->at(t), -ray->direction());
            it.init_phase(_g, swl);
            it.set_medium(_index, _index);
        };
        SampledSpectrum tr = Tr(t, swl);
        SampledSpectrum density = select(sampled_medium, sigma_t * tr, tr);
        Float pdf = density.average();
        SampledSpectrum ret = select(sampled_medium, tr * sigma_s / pdf, tr / pdf);
        return ret;
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::HomogeneousMedium)