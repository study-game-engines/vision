//
// Created by Zero on 09/09/2022.
//

#include "base/shader_graph/shader_node.h"
#include "base/mgr/pipeline.h"

namespace vision {
class Constant : public ShaderNode {
private:
    vector<float> _value;

public:
    explicit Constant(const ShaderNodeDesc &desc)
        : ShaderNode(desc), _value(desc["value"].as_vector<float>()) {}
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    [[nodiscard]] bool is_zero() const noexcept override {
        return std::all_of(_value.begin(), _value.end(), [](float elm) { return elm == 0; });
    }
    [[nodiscard]] uint64_t _compute_type_hash() const noexcept override {
        return hash64_list(_value);
    }
    [[nodiscard]] uint64_t _compute_hash() const noexcept override {
        return hash64_list(_value);
    }
    [[nodiscard]] ocarina::vector<float> average() const noexcept override {
        return _value;
    }
    [[nodiscard]] bool is_constant() const noexcept override { return true; }
    [[nodiscard]] uint dim() const noexcept override { return _value.size(); }
    [[nodiscard]] bool is_uniform() const noexcept override { return true; }
    [[nodiscard]] DynamicArray<float> evaluate(const AttrEvalContext &ctx,
                                        const SampledWavelengths &swl) const noexcept override {
        return DynamicArray<float>(_value);
    }
};
}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::Constant)