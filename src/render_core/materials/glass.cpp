//
// Created by Zero on 05/10/2022.
//

#include "base/scattering/material.h"
#include "base/shader_graph/shader_node.h"
#include "base/mgr/scene.h"
#include "base/color/spd.h"

namespace vision {

class IORCurve {
public:
    [[nodiscard]] virtual Float eta(const Float &lambda) const noexcept = 0;
    [[nodiscard]] virtual float eta(float lambda) const noexcept = 0;
    [[nodiscard]] virtual SampledSpectrum eta(const SampledWavelengths &swl) const noexcept = 0;

    [[nodiscard]] float operator()(float lambda) const noexcept {
        return eta(lambda);
    }
};

#define VS_IOR_CURVE_COMMON                                                                    \
    [[nodiscard]] Float eta(const Float &lambda) const noexcept override {                     \
        return _eta(lambda);                                                                   \
    }                                                                                          \
    [[nodiscard]] float eta(float lambda) const noexcept override {                            \
        return _eta(lambda);                                                                   \
    }                                                                                          \
    [[nodiscard]] SampledSpectrum eta(const SampledWavelengths &swl) const noexcept override { \
        SampledSpectrum ret{swl.dimension()};                                                  \
        for (uint i = 0; i < swl.dimension(); ++i) {                                           \
            ret[i] = eta(swl.lambda(i));                                                       \
        }                                                                                      \
        return ret;                                                                            \
    }

class BK7 : public IORCurve {
private:
    [[nodiscard]] auto _eta(auto lambda) const noexcept {
        using ocarina::sqr;
        using ocarina::sqrt;
        lambda = lambda / 1000.f;
        auto f = 1.03961212f * sqr(lambda) / (sqr(lambda) - 0.00600069867f) +
                 0.231792344f * sqr(lambda) / (sqr(lambda) - 0.0200179144f) +
                 1.01046945f * sqr(lambda) / (sqr(lambda) - 103.560653f);
        return sqrt(f + 1);
    }

public:
    VS_IOR_CURVE_COMMON
};

class LASF9 : public IORCurve {
private:
    [[nodiscard]] auto _eta(auto lambda) const noexcept {
        using ocarina::sqr;
        using ocarina::sqrt;
        lambda = lambda / 1000.f;
        auto f = 2.00029547f * sqr(lambda) / (sqr(lambda) - 0.0121426017f) +
                 0.298926886f * sqr(lambda) / (sqr(lambda) - 0.0538736236f) +
                 1.80691843f * sqr(lambda) / (sqr(lambda) - 156.530829f);
        return sqrt(f + 1);
    }

public:
    VS_IOR_CURVE_COMMON
};

#undef VS_IOR_CURVE_COMMON

[[nodiscard]] static IORCurve *ior_curve(string name) noexcept {
    using Map = map<string, UP<IORCurve>>;
    static Map curve_map = [&]() {
        Map ret;
#define VS_MAKE_GLASS_IOR_CURVE(name) \
    ret[#name] = make_unique<name>();
        VS_MAKE_GLASS_IOR_CURVE(BK7)
        VS_MAKE_GLASS_IOR_CURVE(LASF9)
#undef VS_MAKE_GLASS_IOR_CURVE
        return ret;
    }();
    if (curve_map.find(name) == curve_map.end()) {
        if (name.empty()) {
            return nullptr;
        }
        name = "BK7";
    }
    return curve_map[name].get();
}

class DielectricBxDFSet : public BxDFSet {
private:
    DCSP<Fresnel> _fresnel;
    MicrofacetReflection _refl;
    MicrofacetTransmission _trans;
    Bool _dispersive{};

protected:
    [[nodiscard]] uint64_t _compute_type_hash() const noexcept override {
        return hash64(_fresnel->type_hash(), _refl.type_hash(), _trans.type_hash());
    }

public:
    DielectricBxDFSet(const SP<Fresnel> &fresnel,
                      MicrofacetReflection refl,
                      MicrofacetTransmission trans,
                      const Bool &dispersive)
        : _fresnel(fresnel),
          _refl(ocarina::move(refl)), _trans(ocarina::move(trans)),
          _dispersive(dispersive) {}
    VS_MAKE_BxDFSet_ASSIGNMENT(DielectricBxDFSet)
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _refl.albedo(); }
    [[nodiscard]] optional<Bool> is_dispersive() const noexcept override {
        return _dispersive;
    }
    [[nodiscard]] ScatterEval evaluate_local(Float3 wo, Float3 wi,
                                             Uint flag) const noexcept override {
        ScatterEval ret{_refl.swl().dimension()};
        auto fresnel = _fresnel->clone();
        Float cos_theta_o = cos_theta(wo);
        fresnel->correct_eta(cos_theta_o);
        $if(same_hemisphere(wo, wi)) {
            ret = _refl.evaluate(wo, wi, fresnel);
        }
        $else {
            ret = _trans.evaluate(wo, wi, fresnel);
        };
        return ret;
    }
    [[nodiscard]] SampledDirection sample_wi(Float3 wo, Uint flag,
                                             Sampler *sampler) const noexcept override {
        Float uc = sampler->next_1d();
        auto fresnel = _fresnel->clone();
        Float cos_theta_o = cos_theta(wo);
        fresnel->correct_eta(cos_theta_o);
        Float fr = fresnel->evaluate(abs_cos_theta(wo))[0];
        SampledDirection ret;
        $if(uc < fr) {
            ret = _refl.sample_wi(wo, sampler->next_2d(), fresnel);
            ret.pdf = fr;
        }
        $else {
            ret = _trans.sample_wi(wo, sampler->next_2d(), fresnel);
            ret.pdf = 1 - fr;
        };
        return ret;
    }
    [[nodiscard]] BSDFSample sample_local(Float3 wo, Uint flag,
                                          Sampler *sampler) const noexcept override {
        BSDFSample ret{_refl.swl().dimension()};
        Float uc = sampler->next_1d();
        auto fresnel = _fresnel->clone();
        Float cos_theta_o = cos_theta(wo);
        fresnel->correct_eta(cos_theta_o);
        Float fr = fresnel->evaluate(abs_cos_theta(wo))[0];
        $if(uc < fr) {
            ret = _refl.sample(wo, sampler, fresnel);
            ret.eval.pdf *= fr;
        }
        $else {
            ret = _trans.sample(wo, sampler, fresnel);
            ret.eval.pdf *= 1 - fr;
        };
        return ret;
    }
};

//    "type" : "glass",
//    "param" : {
//        "material_name" : "BK7",
//        "color" : {
//            "channels" : "xyz",
//            "node" : [
//                1,
//                1,
//                1
//            ]
//        },
//        "roughness" : [0.001, 0.001]
//    }
class GlassMaterial : public Material {
private:
    Slot _color{};
    Slot _ior{};
    Slot _roughness{};
    bool _remapping_roughness{true};

protected:
    void _build_evaluator(Material::Evaluator &evaluator, const Interaction &it,
                          const SampledWavelengths &swl) const noexcept override {
        evaluator.link(ocarina::dynamic_unique_pointer_cast<DielectricBxDFSet>(create_lobe_set(it, swl)));
    }

public:
    explicit GlassMaterial(const MaterialDesc &desc)
        : Material(desc),
          _color(scene().create_slot(desc.slot("color", make_float3(1.f), Albedo))),
          _roughness(scene().create_slot(desc.slot("roughness", make_float2(0.01f)))),
          _remapping_roughness(desc["remapping_roughness"].as_bool(true)) {
        init_ior(desc);
        init_slot_cursor(&_color, 3);
    }
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    void init_ior(const MaterialDesc &desc) noexcept {
        auto name = desc["material_name"].as_string();
        SlotDesc eta_slot;
        if (name.empty()) {
            eta_slot = desc.slot("ior", 1.5f);
        } else if (spectrum().is_complete()) {
            eta_slot = SlotDesc(ShaderNodeDesc{ShaderNodeType::ESPD, "spd"}, 0);
            auto lst = SPD::to_list(*ior_curve(name));
            eta_slot.node.set_value("value", lst);
        } else {
            float lambda = rgb_spectrum_peak_wavelengths.x;
            float ior = (*ior_curve(name))(lambda);
            eta_slot = desc.slot("", ior);
        }
        _ior = scene().create_slot(eta_slot);
    }

    void prepare() noexcept override {
        _ior->prepare();
    }

    [[nodiscard]] UP<BxDFSet> create_lobe_set(Interaction it, const SampledWavelengths &swl) const noexcept override {
        SampledSpectrum color = _color.eval_albedo_spectrum(it, swl).sample;
        DynamicArray<float> iors = _ior.evaluate(it, swl);

        Float2 alpha = _roughness.evaluate(it, swl).as_vec2();
        alpha = _remapping_roughness ? roughness_to_alpha(alpha) : alpha;
        alpha = clamp(alpha, make_float2(0.0001f), make_float2(1.f));
        auto microfacet = make_shared<GGXMicrofacet>(alpha.x, alpha.y);
        auto fresnel = make_shared<FresnelDielectric>(SampledSpectrum{iors},
                                                      swl, pipeline());
        MicrofacetReflection refl(SampledSpectrum(swl.dimension(), 1.f), swl, microfacet);
        MicrofacetTransmission trans(color, swl, microfacet);
        return make_unique<DielectricBxDFSet>(fresnel, ocarina::move(refl), ocarina::move(trans), _ior->type() == ESPD);
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::GlassMaterial)