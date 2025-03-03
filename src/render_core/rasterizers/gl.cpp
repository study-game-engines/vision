//
// Created by Zero on 2023/7/10.
//

#include "base/bake_utlis.h"
#include "windows/gl.h"
#include "windows/gl_helper.h"

namespace vision {

struct GLContext {
    GLuint program{0};
    GLuint fb_texture{0};
    GLuint program_tex{0};
    GLuint vao{0};
    GLuint vbo{0};
};

class OpenGLRasterizer : public Rasterizer {
private:
    GLContext _context;
    uint2 _res;

public:
    explicit OpenGLRasterizer(const RasterizerDesc &desc)
        : Rasterizer(desc) {
        init();
    }
    [[nodiscard]] string_view impl_type() const noexcept override { return VISION_PLUGIN_NAME; }
    void init() {

    }

    void compile() noexcept override {
    }

    void apply(vision::BakedShape &baked_shape) noexcept override {

    }
};

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::OpenGLRasterizer)