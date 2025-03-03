//
// Created by Zero on 2023/6/12.
//

#include "pipeline.h"
#include "core/thread_pool.h"

namespace vision {

BakePipeline::BakePipeline(const PipelineDesc &desc)
    : Pipeline(desc),
      _desc(desc) {
    create_cache_directory_if_necessary();
}

void BakePipeline::init_scene(const vision::SceneDesc &scene_desc) {
    _scene.init(scene_desc);
    init_postprocessor(scene_desc.denoiser_desc);
    _postprocessor.set_tone_mapper(_scene.camera()->radiance_film()->tone_mapper());
}

void BakePipeline::init_postprocessor(const DenoiserDesc &desc) {
    _postprocessor.set_denoiser(_scene.load<Denoiser>(desc));
}

void BakePipeline::prepare() noexcept {
    auto pixel_num = resolution().x * resolution().y;
    _final_picture.reset_all(device(), pixel_num);
    _scene.prepare();
    image_pool().prepare();
    preprocess();
    prepare_geometry();
    compile();
    upload_bindless_array();
    _lightmap_base_index = pipeline()->bindless_array().texture_num();
    bake_all();
    update_geometry();
    upload_bindless_array();
}

void BakePipeline::preprocess() noexcept {
    // fill baked shape list
    for_each_need_bake([&](ShapeInstance &item) {
        _baked_shapes.emplace_back(&item);
    });
    SP<UVUnwrapper> uv_unwrapper = Global::node_mgr().load<UVUnwrapper>(_desc.unwrapper_desc);

    // uv unwrap
    VS_BAKER_STATS(_baker_stats, uv_unwrap)
    std::for_each(_baked_shapes.begin(), _baked_shapes.end(), [&](BakedShape &baked_shape) {
        Mesh *mesh = baked_shape.shape()->mesh().get();
        if (mesh->has_lightmap_uv()) {
            return ;
        }
        UnwrapperResult unwrap_result;
        if (baked_shape.has_uv_cache()) {
            unwrap_result = baked_shape.load_uv_config_from_cache();
        } else {
            unwrap_result = uv_unwrapper->apply(baked_shape.shape()->mesh().get());
            baked_shape.save_to_cache(unwrap_result);
        }
        mesh->setup_lightmap_uv(unwrap_result);
    });
}

void BakePipeline::compile() noexcept {
    compile_displayer();
}

void BakePipeline::compile_displayer() noexcept {
    Camera *camera = scene().camera().get();
    Sampler *sampler = scene().sampler();
    Kernel kernel = [&](Uint frame_index, Uint lightmap_base) {
        Uint2 pixel = dispatch_idx().xy();
        sampler->start(pixel, frame_index, 0);
        SensorSample ss = sampler->sensor_sample(pixel, camera->filter());
        camera->load_data();
        RayState rs = camera->generate_ray(ss);

        Float3 L = make_float3(0.f);

        Var hit = geometry().trace_closest(rs.ray);
        $if(hit->is_miss()) {
            camera->radiance_film()->add_sample(pixel, L, frame_index);
            $return();
        };

        Interaction it = geometry().compute_surface_interaction(hit, rs.ray);

        $if(it.has_lightmap()) {
            Float4 t = bindless_array().tex(lightmap_base + it.lightmap_id).sample(4, it.lightmap_uv).as_vec4();
            L = t.xyz() / t.w;
            $if(has_invalid(L)) {
                L = make_float3(0.f);
            };
        };
        camera->radiance_film()->add_sample(pixel, L, frame_index);
    };
    _display_shader = device().compile(kernel, "display");
}

void BakePipeline::bake_all() noexcept {
    Baker baker{_baker_stats, Global::node_mgr().load<Rasterizer>(_desc.rasterizer_desc)};
    baker.allocate();
    baker.compile();
    _baker_stats.set_model_num(_baked_shapes.size());

    async([&] {
        while (_baker_stats.is_valid()) {
            _baker_stats.report_progress();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    _baker_stats.set_spp(scene().sampler()->sample_per_pixel());
    size_t bake_pixel_num = 0;
    std::sort(_baked_shapes.begin(), _baked_shapes.end(),
              [&](const BakedShape &a, const BakedShape &b) {
                  return a.perimeter() > b.perimeter();
              });
    std::for_each(_baked_shapes.begin(), _baked_shapes.end(), [&](const BakedShape &bs) {
        bake_pixel_num += bs.pixel_num();
    });
    _baker_stats.set_pixel_num(bake_pixel_num);

    for (int i = 0; i < _baked_shapes.size(); ++i) {
        BakedShape &bs = _baked_shapes[i];
        bs.shape()->set_lightmap_id(i);
    }

    for (auto iter = _baked_shapes.begin(); iter != _baked_shapes.end();) {
        uint pixel_num = 0;
        auto it = iter;
        for (; it != _baked_shapes.end(); ++it) {
            if (pixel_num + it->pixel_num() <= baker.buffer_size()) {
                pixel_num += it->pixel_num();
            } else {
                break;
            }
        }
        baker.baking(ocarina::span(iter, it));
        iter = it;
    }

    std::for_each(_baked_shapes.begin(), _baked_shapes.end(), [&](BakedShape &bs) {
        bs.normalize_lightmap_uv();
    });

    stream() << baker.deallocate()
             << synchronize() << commit();
    OC_INFO(_baker_stats.get_all_stats());
    _baker_stats.clear();
}

void BakePipeline::render(double dt) noexcept {
    Clock clk;
    stream() << _display_shader(frame_index(),
                                _lightmap_base_index)
                    .dispatch(resolution());
    stream() << synchronize();
    stream() << commit();
    double ms = clk.elapse_ms();
    Env::printer().retrieve_immediately();
    integrator()->accumulate_render_time(ms);
    printf("time consuming (current frame: %.3f, average: %.3f) frame index: %u    \r", ms, render_time() / frame_index(), frame_index());
}

void BakePipeline::display(double dt) noexcept {
    render(dt);
}

}// namespace vision

VS_MAKE_CLASS_CREATOR(vision::BakePipeline)