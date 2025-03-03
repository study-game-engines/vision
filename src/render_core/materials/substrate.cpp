//
// Created by Zero on 28/10/2022.
//

#include "base/scattering/material.h"
#include "base/shader_graph/shader_node.h"
#include "base/mgr/scene.h"
#include "math/warp.h"

namespace vision {

class FresnelBlend : public BxDF {
private:
    SampledSpectrum Rd, Rs;
    DCSP<Microfacet<D>> _microfacet;

public:
    FresnelBlend(SampledSpectrum Rd, SampledSpectrum Rs, const SampledWavelengths &swl, const SP<Microfacet<D>> &m)
        : BxDF(swl, BxDFFlag::Reflection), Rd(Rd), Rs(Rs), _microfacet(m) {}
    // clang-format off
    VS_MAKE_BxDF_ASSIGNMENT(FresnelBlend)
        // clang-format on
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return Rd; }

    [[nodiscard]] SampledSpectrum f_diffuse(Float3 wo, Float3 wi) const noexcept {
        SampledSpectrum diffuse = (28.f / (23.f * Pi)) * Rd * (SampledSpectrum(swl().dimension(), 1.f) - Rs) *
                                  (1 - Pow<5>(1 - .5f * abs_cos_theta(wi))) *
                                  (1 - Pow<5>(1 - .5f * abs_cos_theta(wo)));
        return diffuse;
    }
    [[nodiscard]] Float PDF_diffuse(Float3 wo, Float3 wi) const noexcept {
        return cosine_hemisphere_PDF(abs_cos_theta(wi));
    }
    [[nodiscard]] SampledSpectrum f_specular(Float3 wo, Float3 wi) const noexcept {
        Float3 wh = wi + wo;
        wh = normalize(wh);
        SampledSpectrum specular = _microfacet->D_(wh) / (4 * abs_dot(wi, wh) * max(abs_cos_theta(wi), abs_cos_theta(wo))) *
                                   fresnel_schlick(Rs, dot(wi, wh));
        return select(is_zero(wh), 0.f, 1.f) * specular;
    }
    [[nodiscard]] Float PDF_specular(Float3 wo, Float3 wi) const noexcept {
        Float3 wh = normalize(wo + wi);
        Float ret = _microfacet->PDF_wi_reflection(wo, wh);
        return ret;
    }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        SampledSpectrum ret = f_specular(wo, wi) + f_diffuse(wo, wi);
        return ret;
    }
    [[nodiscard]] Float PDF(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        Float fr = fresnel->evaluate(abs_cos_theta(wo))[0];
        return lerp(fr, PDF_diffuse(wo, wi), PDF_specular(wo, wi));
    }

    [[nodiscard]] SampledDirection sample_wi(Float3 wo, Float2 u, SP<Fresnel> fresnel) const noexcept override {
        SampledDirection ret;
        Float fr = fresnel->evaluate(abs_cos_theta(wo))[0];
        $if(u.x < fr) {
            u.x = remapping(u.x, 0.f, fr);
            Float3 wh = _microfacet->sample_wh(wo, u);
            ret.wi = reflect(wo, wh);
        }
        $else {
            u.x = remapping(u.x, fr, 1.f);
            ret.wi = square_to_cosine_hemisphere(u);
            ret.wi.z = select(wo.z < 0, -ret.wi.z, ret.wi.z);
        };
        ret.pdf = 1.f;
        return ret;
    }
};

class SubstrateBxDFSet : public BxDFSet {
private:
    SP<Fresnel> _fresnel;
    FresnelBlend _bxdf;

protected:
    [[nodiscard]] uint64_t _compute_type_hash() const noexcept override {
        return hash64(_bxdf.type_hash(), _fresnel->type_hash());
    }

public:
    SubstrateBxDFSet(const SP<Fresnel> &fresnel, FresnelBlend bxdf)
        : _fresnel(fresnel), _bxdf(std::move(bxdf)) {}
    VS_MAKE_BxDFSet_ASSIGNMENT(SubstrateBxDFSet)
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _bxdf.albedo(); }
    [[nodiscard]] ScatterEval evaluate_local(Float3 wo, Float3 wi, Uint flag) const noexcept override {
        return _bxdf.safe_evaluate(wo, wi, _fresnel->clone());
    }
    [[nodiscard]] BSDFSample sample_local(Float3 wo, Uint flag, Sampler *sampler) const noexcept override {
        return _bxdf.sample(wo, sampler, _fresnel->clone());
    }
    [[nodiscard]] SampledDirection sample_wi(Float3 wo, Uint flag,
                                             Sampler *sampler) const noexcept override {
        return _bxdf.sample_wi(wo, sampler->next_2d(), _fresnel->clone());
    }
};

//    "type" : "substrate",
//    "param" : {
//        "roughness" : 0.001,
//        "spec" : [
//            0.04,
//            0.04,
//            0.04
//        ],
//        "color" : [
//            0.725,
//            0.71,
//            0.68
//        ]
//    }
class SubstrateMaterial : public Material {
private:
    Slot _diff{};
    Slot _spec{};
    Slot _roughness{};
    bool _remapping_roughness{true};

protected:
    void _build_evaluator(Material::Evaluator &evaluator, const Interaction &it,
                          const SampledWavelengths &swl) const noexcept override {
        evaluator.link(ocarina::dynamic_unique_pointer_cast<SubstrateBxDFSet>(create_lobe_set(it, swl)));
    }

public:
    explicit SubstrateMaterial(const MaterialDesc &desc)
        : Material(desc), _diff(scene().create_slot(desc.slot("color", make_float3(1.f), Albedo))),
          _spec(scene().create_slot(desc.slot("spec", make_float3(0.05f), Albedo))),
          _roughness(scene().create_slot(desc.slot("roughness", make_float2(0.001f)))),
          _remapping_roughness(desc["remapping_roughness"].as_bool(true)) {
        init_slot_cursor(&_diff, 3);
    }
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }

    [[nodiscard]] UP<BxDFSet> create_lobe_set(Interaction it, const SampledWavelengths &swl) const noexcept override {
        SampledSpectrum Rd = _diff.eval_albedo_spectrum(it, swl).sample;
        SampledSpectrum Rs = _spec.eval_albedo_spectrum(it, swl).sample;
        Float2 alpha = _roughness.evaluate(it, swl).as_vec2();
        alpha = _remapping_roughness ? roughness_to_alpha(alpha) : alpha;
        alpha = clamp(alpha, make_float2(0.0001f), make_float2(1.f));
        auto microfacet = make_shared<GGXMicrofacet>(alpha.x, alpha.y);
        auto fresnel = make_shared<FresnelDielectric>(SampledSpectrum{swl.dimension(), 1.5f},
                                                      swl, pipeline());
        FresnelBlend bxdf(Rd, Rs, swl, microfacet);
        return make_unique<SubstrateBxDFSet>(fresnel, ocarina::move(bxdf));
    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::SubstrateMaterial)