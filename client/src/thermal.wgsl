// The thermal sight's heat: the creatures in sight, as the thermal camera drew them, become a
// white-hot outline around a faint warm fill. Only the drawn coverage (alpha) is read.
#import bevy_ui::ui_vertex_output::UiVertexOutput

@group(1) @binding(0) var mask: texture_2d<f32>;
@group(1) @binding(1) var mask_sampler: sampler;
// xy: one texel in uv; z: the fill's alpha; w: the outline's width in texels.
@group(1) @binding(2) var<uniform> params: vec4<f32>;

@fragment
fn fragment(in: UiVertexOutput) -> @location(0) vec4<f32> {
    let here = textureSample(mask, mask_sampler, in.uv).a;
    let r = params.xy * params.w;
    var most = here;
    var least = here;
    // Eight neighbours at the outline's width: where coverage changes, the edge glows.
    let dirs = array<vec2<f32>, 8>(
        vec2<f32>(1.0, 0.0), vec2<f32>(-1.0, 0.0), vec2<f32>(0.0, 1.0), vec2<f32>(0.0, -1.0),
        vec2<f32>(0.7071, 0.7071), vec2<f32>(-0.7071, 0.7071), vec2<f32>(0.7071, -0.7071),
        vec2<f32>(-0.7071, -0.7071),
    );
    for (var i = 0; i < 8; i = i + 1) {
        let a = textureSample(mask, mask_sampler, in.uv + dirs[i] * r).a;
        most = max(most, a);
        least = min(least, a);
    }
    let edge = clamp(most - least, 0.0, 1.0);
    let alpha = max(edge, here * params.z);
    return vec4<f32>(1.0, 0.97, 0.9, alpha);
}
