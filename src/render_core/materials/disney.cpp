//
// Created by Zero on 28/10/2022.
//

#include "base/scattering/material.h"
#include "base/shader_graph/shader_node.h"
#include "base/mgr/scene.h"

namespace vision {

inline namespace disney {

class Diffuse : public BxDF {
private:
    SampledSpectrum _color;

public:
    Diffuse() = default;
    explicit Diffuse(SampledSpectrum color, const SampledWavelengths &swl)
        : BxDF(swl, BxDFFlag::DiffRefl),
          _color(color) {}
    // clang-format off
    VS_MAKE_BxDF_ASSIGNMENT(Diffuse)
        // clang-format on
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _color; }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi) {
            Float Fo = schlick_weight(abs_cos_theta(wo));
            Float Fi = schlick_weight(abs_cos_theta(wi));
            return InvPi * (1 - 0.5f * Fo) * (1 - 0.5f * Fi);
        };
        impl.function()->set_description("disney::Diffuse::f");
        return _color * impl(wi, wi);
    }
};

class FakeSS : public BxDF {
private:
    SampledSpectrum _color;
    Float _roughness;

public:
    FakeSS() = default;
    explicit FakeSS(SampledSpectrum color, Float r, const SampledWavelengths &swl)
        : BxDF(swl, BxDFFlag::DiffRefl),
          _color(color),
          _roughness(r) {}
    // clang-format off
    VS_MAKE_BxDF_ASSIGNMENT(FakeSS)
        // clang-format on
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _color; }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi, Float roughness) {
            Float3 wh = wi + wo;
            Bool valid = !is_zero(wh);
            wh = normalize(wh);
            Float cos_theta_d = dot(wi, wh);
            Float Fss90 = sqr(cos_theta_d) * roughness;
            Float Fo = schlick_weight(abs_cos_theta(wo));
            Float Fi = schlick_weight(abs_cos_theta(wi));
            Float Fss = lerp(Fo, 1.f, Fss90) * lerp(Fi, 1.f, Fss90);
            Float ss = 1.25f * (Fss * (1 / (abs_cos_theta(wo) + abs_cos_theta(wi)) - .5f) + .5f);
            Float ret = InvPi * ss;
            return select(valid, ret, 0.f);
        };
        impl.function()->set_description("disney::FakeSS::f");
        return _color * impl(wo, wi, _roughness);
    }
};

class Retro : public BxDF {
private:
    SampledSpectrum _color;
    Float _roughness;

public:
    VS_MAKE_BxDF_ASSIGNMENT(Retro)
        Retro() = default;
    explicit Retro(SampledSpectrum color, Float r, const SampledWavelengths &swl)
        : BxDF(swl, BxDFFlag::DiffRefl),
          _color(color),
          _roughness(r) {}
    [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _color; }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi, Float roughness) {
            Float3 wh = wi + wo;
            Bool valid = !is_zero(wh);
            wh = normalize(wh);
            Float cos_theta_d = dot(wi, wh);

            Float Fo = schlick_weight(abs_cos_theta(wo));
            Float Fi = schlick_weight(abs_cos_theta(wi));
            Float Rr = 2 * roughness * sqr(cos_theta_d);

            Float ret = InvPi * Rr * (Fo + Fi + Fo * Fi * (Rr - 1));
            return select(valid, ret, 0.f);
        };
        impl.function()->set_description("disney::Retro::f");
        return _color * impl(wo, wi, _roughness);
    }
};

class Sheen : public BxDF {
private:
    SampledSpectrum _color;

public:
    VS_MAKE_BxDF_ASSIGNMENT(Sheen)
        Sheen() = default;
    explicit Sheen(SampledSpectrum kr, const SampledWavelengths &swl)
        : BxDF(swl, BxDFFlag::DiffRefl),
          _color(kr) {}
    [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _color; }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi) {
            Float3 wh = wi + wo;
            Bool valid = !is_zero(wh);
            wh = normalize(wh);
            Float cos_theta_d = dot(wi, wh);
            Float ret = schlick_weight(cos_theta_d);
            return select(valid, ret, 0.f);
        };
        impl.function()->set_description("disney::Sheen::f");
        return _color * impl(wo, wi);
    }
};

[[nodiscard]] Float GTR1(Float cos_theta, Float alpha) {
    static CALLABLE_TYPE impl = [](Float cos_theta, Float alpha) {
        Float alpha2 = sqr(alpha);
        return (alpha2 - 1) /
               (Pi * log(alpha2) * (1 + (alpha2 - 1) * sqr(cos_theta)));
    };
    impl.function()->set_description("disney::GTR1");
    return impl(cos_theta, alpha);
}

[[nodiscard]] Float smithG_GGX(Float cos_theta, Float alpha) {
    static CALLABLE_TYPE impl = [](Float cos_theta, Float alpha) {
        Float alpha2 = sqr(alpha);
        Float cos_theta_2 = sqr(cos_theta);
        return 1 / (cos_theta + sqrt(alpha2 + cos_theta_2 - alpha2 * cos_theta_2));
    };
    impl.function()->set_description("disney::smithG_GGX");
    return impl(cos_theta, alpha);
}

class Clearcoat : public BxDF {
private:
    Float _weight;
    Float _alpha;

public:
    VS_MAKE_BxDF_ASSIGNMENT(Clearcoat)
        Clearcoat() = default;
    Clearcoat(Float weight, Float alpha, const SampledWavelengths &swl)
        : BxDF(swl, BxDFFlag::GlossyRefl),
          _weight(weight),
          _alpha(alpha) {}
    [[nodiscard]] SampledSpectrum albedo() const noexcept override { return {swl().dimension(), _weight}; }
    [[nodiscard]] SampledSpectrum f(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi, Float weight, Float alpha) {
            Float3 wh = wi + wo;
            Bool valid = !is_zero(wh);
            wh = normalize(wh);
            Float Dr = GTR1(abs_cos_theta(wh), alpha);
            Float Fr = fresnel_schlick<D>(0.04f, dot(wo, wh));
            Float Gr = smithG_GGX(abs_cos_theta(wo), 0.25f) * smithG_GGX(abs_cos_theta(wi), 0.25f);
            Float ret = weight * Gr * Fr * Dr * 0.25f;
            return select(valid, ret, 0.f);
        };
        impl.function()->set_description("disney::Clearcoat::f");
        return {swl().dimension(), impl(wo, wi, _weight, _alpha)};
    }
    [[nodiscard]] Float PDF(Float3 wo, Float3 wi, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wi, Float alpha) {
            Float3 wh = wi + wo;
            Bool valid = !is_zero(wh);
            wh = normalize(wh);
            Float Dr = GTR1(abs_cos_theta(wh), alpha);
            Float ret = Dr * abs_cos_theta(wh) / (4 * dot(wo, wh));
            return select(valid, ret, 0.f);
        };
        impl.function()->set_description("disney::Clearcoat::PDF");
        return impl(wo, wi, _alpha);
    }
    [[nodiscard]] SampledDirection sample_wi(Float3 wo, Float2 u, SP<Fresnel> fresnel) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float2 u, Float alpha) {
            Float alpha2 = sqr(alpha);
            Float cos_theta = safe_sqrt((1 - pow(alpha2, 1 - u[0])) / (1 - alpha2));
            Float sin_theta = safe_sqrt(1 - sqr(cos_theta));
            Float phi = 2 * Pi * u[1];
            Float3 wh = spherical_direction(sin_theta, cos_theta, phi);
            wh = select(same_hemisphere(wo, wh), wh, -wh);
            return reflect(wo, wh);
        };
        impl.function()->set_description("disney::Clearcoat::sample_wi");
        Float3 wi = impl(wo, u, _alpha);
        return {wi, 1.f};
    }

    [[nodiscard]] BSDFSample sample(Float3 wo, Sampler *sampler, SP<Fresnel> fresnel) const noexcept override {
        Float2 u = sampler->next_2d();
        auto [wi, pdf] = sample_wi(wo, u, fresnel);
        BSDFSample ret{swl().dimension()};
        ret.eval = safe_evaluate(wo, wi, nullptr);
        ret.wi = wi;
        return ret;
    }
};

class FresnelDisney : public Fresnel {
private:
    SampledSpectrum R0;
    Float _metallic;
    Float _eta;

public:
    FresnelDisney(const SampledSpectrum &R0, Float metallic, Float eta,
                  const SampledWavelengths &swl, const Pipeline *rp)
        : Fresnel(swl, rp), R0(R0), _metallic(metallic), _eta(eta) {}
    void correct_eta(Float cos_theta) noexcept override {
        _eta = select(cos_theta > 0, _eta, rcp(_eta));
    }
    [[nodiscard]] SampledSpectrum evaluate(Float cos_theta) const noexcept override {
        return lerp(_metallic,
                    fresnel_dielectric(cos_theta, _eta),
                    fresnel_schlick(R0, cos_theta));
    }
    [[nodiscard]] SampledSpectrum eta() const noexcept override { return {_swl->dimension(), _eta}; }
    [[nodiscard]] SP<Fresnel> clone() const noexcept override {
        return make_shared<FresnelDisney>(R0, _metallic, _eta, *_swl, _rp);
    }
    VS_MAKE_Fresnel_ASSIGNMENT(FresnelDisney)
};

}// namespace disney

class DisneyMicrofacet : public Microfacet<D> {
private:
    static constexpr MicrofacetType type = MicrofacetType::Disney;
    using Super = Microfacet<D>;

public:
    explicit DisneyMicrofacet(Float2 alpha) : Super(alpha, type) {}
    DisneyMicrofacet(Float ax, Float ay) : Super(ax, ay, type) {}

    [[nodiscard]] Float D_(Float3 wh) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wh, Float ax, Float ay) {
            return microfacet::D_<D>(wh, ax, ay, type);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::D");
        return impl(wh, _alpha_x, _alpha_y);
    }
    [[nodiscard]] Float3 sample_wh(const Float3 &wo, const Float2 &u) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float2 u, Float ax, Float ay) {
            return microfacet::sample_wh<D>(wo, u, ax, ay, type);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::sample_wh");
        return impl(wo, u, _alpha_x, _alpha_y);
    }
    [[nodiscard]] Float PDF_wh(const Float3 &wo, const Float3 &wh) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wh, Float ax, Float ay) {
            return microfacet::PDF_wh<D>(wo, wh, ax, ay, type);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::PDF_wh");
        return impl(wo, wh, _alpha_x, _alpha_y);
    }

    [[nodiscard]] Float PDF_wi_reflection(Float pdf_wh, Float3 wo, Float3 wh) const noexcept override {
        static CALLABLE_TYPE impl = [](Float pdf_wh, Float3 wo, Float3 wh) {
            return microfacet::PDF_wi_reflection<D>(pdf_wh, wo, wh);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::PDF_wi_reflection");
        return impl(pdf_wh, wo, wh);
    }

    [[nodiscard]] Float PDF_wi_reflection(Float3 wo, Float3 wh) const noexcept override {
        return PDF_wi_reflection(PDF_wh(wo, wh), wo, wh);
    }

    [[nodiscard]] Float PDF_wi_transmission(Float pdf_wh, Float3 wo, Float3 wh,
                                            Float3 wi, Float eta) const noexcept override {
        static CALLABLE_TYPE impl = [](Float pdf_wh, Float3 wo, Float3 wh, Float3 wi, Float eta) {
            return microfacet::PDF_wi_transmission<D>(pdf_wh, wo, wh, wi, eta);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::PDF_wi_transmission");
        return impl(pdf_wh, wo, wh, wi, eta);
    }

    [[nodiscard]] Float PDF_wi_transmission(Float3 wo, Float3 wh, Float3 wi, Float eta) const noexcept override {
        return PDF_wi_transmission(PDF_wh(wo, wh), wo, wh, wi, eta);
    }

    [[nodiscard]] SampledSpectrum BRDF(Float3 wo, Float3 wh, Float3 wi, const SampledSpectrum &Fr) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wh, Float3 wi, Float ax, Float ay) {
            return microfacet::BRDF_div_fr<D>(wo, wh, wi, ax, ay, type);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::BRDF_div_fr");
        return impl(wo, wh, wi, _alpha_x, _alpha_y) * Fr;
    }

    [[nodiscard]] SampledSpectrum BRDF(Float3 wo, Float3 wi, const SampledSpectrum &Fr) const noexcept override {
        Float3 wh = normalize(wo + wi);
        return BRDF(wo, wh, wi, Fr);
    }

    [[nodiscard]] SampledSpectrum BTDF(Float3 wo, Float3 wh, Float3 wi,
                                       const SampledSpectrum &Ft, Float eta) const noexcept override {
        static CALLABLE_TYPE impl = [](Float3 wo, Float3 wh, Float3 wi, Float eta, Float ax, Float ay) {
            return microfacet::BTDF_div_ft<D>(wo, wh, wi, eta, ax, ay, type);
        };
        impl.function()->set_description("disney::DisneyMicrofacet::BTDF_div_ft");
        return impl(wo, wh, wi, eta, _alpha_x, _alpha_y) * Ft;
    }

    [[nodiscard]] SampledSpectrum BTDF(Float3 wo, Float3 wi, const SampledSpectrum &Ft, Float eta) const noexcept override {
        Float3 wh = normalize(wo + wi * eta);
        return BTDF(wo, wh, wi, Ft, eta);
    }
};

class PrincipledBxDFSet : public BxDFSet {
private:
    const SampledWavelengths *_swl{};
    DCSP<Fresnel> _fresnel{};
    optional<Diffuse> _diffuse{};
    optional<Retro> _retro{};
    optional<Sheen> _sheen{};
    optional<FakeSS> _fake_ss{};
    optional<MicrofacetReflection> _spec_refl{};
    optional<Clearcoat> _clearcoat{};
    optional<MicrofacetTransmission> _spec_trans{};

    // sampling strategy
    static constexpr size_t max_sampling_strategy_num = 4u;
    array<Float, max_sampling_strategy_num> _sampling_weights;
    uint _diffuse_index{InvalidUI32};
    uint _spec_refl_index{InvalidUI32};
    uint _clearcoat_index{InvalidUI32};
    uint _spec_trans_index{InvalidUI32};
    uint _sampling_strategy_num{0u};

protected:
    [[nodiscard]] uint64_t _compute_type_hash() const noexcept override {
        return hash64(_fresnel->type_hash(), _diffuse.has_value(),
                      _retro.has_value(), _sheen.has_value(),
                      _fake_ss.has_value(), _spec_refl.has_value(),
                      _clearcoat.has_value(), _spec_trans.has_value(),
                      _diffuse_index, _spec_refl_index,
                      _clearcoat_index, _spec_trans_index,
                      _sampling_strategy_num);
    }

private:
    template<typename T, typename... Args>
    [[nodiscard]] SampledSpectrum lobe_f(const optional<T> &lobe, Args &&...args) const noexcept {
        if (lobe.has_value()) {
            return OC_FORWARD(lobe)->f(OC_FORWARD(args)...);
        }
        return SampledSpectrum(_swl->dimension(), 0.f);
    }

    template<typename T, typename... Args>
    [[nodiscard]] Float lobe_PDF(const optional<T> &lobe, Args &&...args) const noexcept {
        if (lobe.has_value()) {
            return OC_FORWARD(lobe)->PDF(OC_FORWARD(args)...);
        }
        return 0.f;
    }

    template<typename... Args>
    [[nodiscard]] SampledSpectrum f_diffuse(Args &&...args) const noexcept {
        return lobe_f(_diffuse, OC_FORWARD(args)...) + lobe_f(_retro, OC_FORWARD(args)...) +
               lobe_f(_sheen, OC_FORWARD(args)...) + lobe_f(_fake_ss, OC_FORWARD(args)...);
    }

    template<typename... Args>
    [[nodiscard]] Float PDF_diffuse(Args &&...args) const noexcept {
        return lobe_PDF(_diffuse, OC_FORWARD(args)...);
    }

public:
    PrincipledBxDFSet(const Interaction &it, const SampledWavelengths &swl, const Pipeline *rp, Slot color_slot,
                      Slot metallic_slot, Slot eta_slot, Slot roughness_slot,
                      Slot spec_tint_slot, Slot anisotropic_slot, Slot sheen_slot,
                      Slot sheen_tint_slot, Slot clearcoat_slot, Slot clearcoat_alpha_slot,
                      Slot spec_trans_slot, Slot flatness_slot, Slot diff_trans_slot) : _swl(&swl) {

        auto [color, color_lum] = color_slot.eval_albedo_spectrum(it, swl);
        Float metallic = metallic_slot.evaluate(it, swl).as_scalar();
        Float spec_trans = spec_trans_slot.evaluate(it, swl).as_scalar();
        Float diffuse_weight = (1.f - metallic) * (1 - spec_trans);
        Float flatness = flatness_slot.evaluate(it, swl).as_scalar();
        Float roughness = roughness_slot.evaluate(it, swl).as_scalar();
        Float tint_weight = select(color_lum > 0.f, 1.f / color_lum, 1.f);
        SampledSpectrum tint = clamp(color * tint_weight, 0.f, 1.f);
        Float tint_lum = color_lum * tint_weight;

        Float Cdiff_weight = diffuse_weight * (1.f - flatness);
        SampledSpectrum Cdiff = color * Cdiff_weight;

        bool has_diffuse = false;

        if (!color_slot->is_zero()) {
            _diffuse = Diffuse(Cdiff, swl);
            _retro = Retro(Cdiff, roughness, swl);
            has_diffuse = true;
        }

        if (!flatness_slot->is_zero()) {
            Float Css_weight = diffuse_weight * flatness;
            SampledSpectrum Css = Css_weight * color;
            _fake_ss = FakeSS(Css, roughness, swl);
            has_diffuse = true;
        }

        if (!sheen_slot->is_zero()) {
            Float sheen = sheen_slot.evaluate(it, swl).as_scalar();
            Float sheen_tint = sheen_tint_slot.evaluate(it, swl).as_scalar();
            Float Csheen_weight = diffuse_weight * sheen;
            SampledSpectrum Csheen = Csheen_weight * lerp(sheen_tint, 1.f, tint);
            _sheen = Sheen(Csheen, swl);
            has_diffuse = true;
        }

        if (has_diffuse) {
            _diffuse_index = _sampling_strategy_num++;
            _sampling_weights[_diffuse_index] = saturate(diffuse_weight * color_lum);
        }

        Float spec_tint = spec_tint_slot.evaluate(it, swl).as_scalar();
        Float eta = eta_slot.evaluate(it, swl).as_scalar();
        Float SchlickR0 = schlick_R0_from_eta(eta);
        SampledSpectrum Cspec0 = lerp(metallic, lerp(spec_tint, 1.f, tint) * SchlickR0, color);

        _fresnel = make_deep_copy_shared<FresnelDisney>(Cspec0, metallic, eta, swl, rp);
        Float anisotropic = anisotropic_slot.evaluate(it, swl).as_scalar();
        Float aspect = sqrt(1 - anisotropic * 0.9f);
        Float2 alpha = make_float2(max(0.001f, sqr(roughness) / aspect),
                                   max(0.001f, sqr(roughness) * aspect));
        auto microfacet = make_shared<DisneyMicrofacet>(alpha);
        _spec_refl = MicrofacetReflection(SampledSpectrum(swl.dimension(), 1.f), swl, microfacet);
        Float Cspec0_lum = lerp(metallic, lerp(spec_tint, 1.f, tint_lum) * SchlickR0, color_lum);
        _spec_refl_index = _sampling_strategy_num++;
        _sampling_weights[_spec_refl_index] = saturate(Cspec0_lum);

        if (!clearcoat_slot->is_zero()) {
            Float cc = clearcoat_slot.evaluate(it, swl).as_scalar();
            Float cc_alpha = lerp(clearcoat_alpha_slot.evaluate(it, swl).as_scalar(), 0.001f, 1.f);
            _clearcoat = Clearcoat(cc, cc_alpha, swl);
            _clearcoat_index = _sampling_strategy_num++;
            _sampling_weights[_clearcoat_index] = saturate(cc * fresnel_schlick(0.04f, 1.f));
        }

        if (!spec_trans_slot->is_zero()) {
            Float Cst_weight = (1.f - metallic) * spec_trans;
            SampledSpectrum Cst = Cst_weight * sqrt(color);
            _spec_trans = MicrofacetTransmission(Cst, swl, microfacet);
            Float Cst_lum = Cst_weight * sqrt(color_lum);
            _spec_trans_index = _sampling_strategy_num++;
            _sampling_weights[_spec_trans_index] = saturate(Cst_lum);
        }

        Float sum_weights = 0.f;
        for (uint i = 0u; i < _sampling_strategy_num; i++) {
            sum_weights += _sampling_weights[i];
        }
        Float inv_sum_weights = select(sum_weights == 0.f, 0.f, 1.f / sum_weights);
        for (uint i = 0u; i < _sampling_strategy_num; i++) {
            _sampling_weights[i] *= inv_sum_weights;
        }
    }
    VS_MAKE_BxDFSet_ASSIGNMENT(PrincipledBxDFSet)
        [[nodiscard]] SampledSpectrum albedo() const noexcept override { return _diffuse->albedo(); }
    [[nodiscard]] ScatterEval evaluate_local(Float3 wo, Float3 wi, Uint flag) const noexcept override {
        ScatterEval ret{_spec_refl->swl().dimension()};
        outline([&] {
            SampledSpectrum f = {_spec_refl->swl().dimension(), 0.f};
            Float pdf = 0.f;
            auto fresnel = _fresnel->clone();
            Float cos_theta_o = cos_theta(wo);
            fresnel->correct_eta(cos_theta_o);
            $if(same_hemisphere(wo, wi)) {
                if (_diffuse_index != InvalidUI32) {
                    f = f_diffuse(wo, wi, fresnel);
                    ret.flags = BxDFFlag::DiffRefl;
                    pdf = _sampling_weights[_diffuse_index] * PDF_diffuse(wo, wi, fresnel);
                }

                f += _spec_refl->f(wo, wi, fresnel);
                pdf += _sampling_weights[_spec_refl_index] * _spec_refl->PDF(wo, wi, fresnel);
                ret.flags = BxDFFlag::GlossyRefl;
                if (_clearcoat.has_value()) {
                    f += _clearcoat->f(wo, wi, fresnel);
                    pdf += _sampling_weights[_clearcoat_index] * _clearcoat->PDF(wo, wi, fresnel);
                }
            }
            $else {
                if (_spec_trans.has_value()) {
                    f = _spec_trans->f(wo, wi, fresnel);
                    ret.flags = BxDFFlag::GlossyTrans;
                    pdf = _sampling_weights[_spec_trans_index] * _spec_trans->PDF(wo, wi, fresnel);
                }
            };
            ret.f = f;
            ret.pdf = pdf;
        },
                "PrincipledBxDFSet::evaluate_local");
        return ret;
    }

    [[nodiscard]] SampledDirection sample_wi(Float3 wo, Uint flag, Sampler *sampler) const noexcept override {
        return outline([&] {
            Float uc = sampler->next_1d();
            Float2 u = sampler->next_2d();
            SampledDirection sampled_direction;

            Uint sampling_strategy = 0u;
            Float sum_weights = 0.f;
            for (uint i = 0; i < _sampling_strategy_num; ++i) {
                sampling_strategy = select(uc > sum_weights, i, sampling_strategy);
                sum_weights += _sampling_weights[i];
            }
            auto fresnel = _fresnel->clone();
            Float cos_theta_o = cos_theta(wo);
            fresnel->correct_eta(cos_theta_o);

            $switch(sampling_strategy) {
                if (_diffuse.has_value()) {
                    $case(_diffuse_index) {
                        sampled_direction = _diffuse->sample_wi(wo, u, fresnel);
                        $break;
                    };
                }
                $case(_spec_refl_index) {
                    sampled_direction = _spec_refl->sample_wi(wo, u, fresnel);
                    $break;
                };
                if (_clearcoat.has_value()) {
                    $case(_clearcoat_index) {
                        sampled_direction = _clearcoat->sample_wi(wo, u, fresnel);
                        $break;
                    };
                }
                if (_spec_trans.has_value()) {
                    $case(_spec_trans_index) {
                        sampled_direction = _spec_trans->sample_wi(wo, u, fresnel);
                        $break;
                    };
                }
                $default {
                    unreachable();
                    $break;
                };
            };
            return sampled_direction;
        },
                       "PrincipledBxDFSet::sample_wi");
    }

    [[nodiscard]] BSDFSample sample_local(Float3 wo, Uint flag, Sampler *sampler) const noexcept override {
        BSDFSample ret{_spec_refl->swl().dimension()};
        SampledDirection sampled_direction = sample_wi(wo, flag, sampler);
        ret.eval = evaluate_local(wo, sampled_direction.wi, flag);
        ret.wi = sampled_direction.wi;
        ret.eval.pdf = select(sampled_direction.valid(), ret.eval.pdf * sampled_direction.pdf, 0.f);
        return ret;
    }
};

//    "type" : "disney",
//    "param" : {
//        "color" : [
//            1,1,1
//        ],
//        "ior" : 1.5,
//        "roughness" : 0.1,
//        "metallic" : 1,
//        "spec_tint" : 0.0,
//        "anisotropic" : 0,
//        "sheen" : 0,
//        "sheen_tint" : 0,
//        "clearcoat" : 0,
//        "clearcoat_alpha" : 0.1,
//        "spec_trans" : 1,
//        "flatness" : 0.98,
//        "scatter_distance" : [0, 0, 0],
//        "diff_trans" : 0.11
//    }
class DisneyMaterial : public Material {
private:
    Slot _color{};
    Slot _metallic{};
    Slot _eta{};
    Slot _roughness{};
    Slot _spec_tint{};
    Slot _anisotropic{};
    Slot _sheen{};
    Slot _sheen_tint{};
    Slot _clearcoat{};
    Slot _clearcoat_alpha{};
    Slot _spec_trans{};
    Slot _flatness{};
    Slot _diff_trans{};
    bool _thin{false};

protected:
    void _build_evaluator(Material::Evaluator &evaluator, const Interaction &it,
                          const SampledWavelengths &swl) const noexcept override {
        evaluator.link(ocarina::dynamic_unique_pointer_cast<PrincipledBxDFSet>(create_lobe_set(it, swl)));
    }

public:
    explicit DisneyMaterial(const MaterialDesc &desc)
        : Material(desc), _color(scene().create_slot(desc.slot("color", make_float3(1.f), Albedo))),
          _metallic(scene().create_slot(desc.slot("metallic", 0.f, Number))),
          _eta(scene().create_slot(desc.slot("ior", 1.5f, Number))),
          _roughness(scene().create_slot(desc.slot("roughness", 0.5f, Number))),
          _spec_tint(scene().create_slot(desc.slot("spec_tint", 0.f, Number))),
          _anisotropic(scene().create_slot(desc.slot("anisotropic", 0.f, Number))),
          _sheen(scene().create_slot(desc.slot("sheen", 0.f, Number))),
          _sheen_tint(scene().create_slot(desc.slot("sheen_tint", 0.f, Number))),
          _clearcoat(scene().create_slot(desc.slot("clearcoat", 0.3f, Number))),
          _clearcoat_alpha(scene().create_slot(desc.slot("clearcoat_alpha", 0.2f, Number))),
          _spec_trans(scene().create_slot(desc.slot("spec_trans", 0.f, Number))),
          _flatness(scene().create_slot(desc.slot("flatness", 0.f, Number))),
          _diff_trans(scene().create_slot(desc.slot("diff_trans", 0.f, Number))) {
        init_slot_cursor(&_color, &_diff_trans);
    }
    [[nodiscard]] UP<BxDFSet> create_lobe_set(Interaction it, const SampledWavelengths &swl) const noexcept override {
        return make_unique<PrincipledBxDFSet>(it, swl, pipeline(), _color, _metallic,
                                              _eta, _roughness, _spec_tint, _anisotropic,
                                              _sheen, _sheen_tint, _clearcoat, _clearcoat_alpha,
                                              _spec_trans, _flatness, _diff_trans);
    }
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::DisneyMaterial)