/*
    The 2D overlay's fragment stage - a rounded box and a glyph, in one expression, with no branch.

    No #version here either; see ui_overlay.vert's header for what that means and why.

    --- THE WHOLE IDEA ---------------------------------------------------------------------------
    A rounded rectangle and a glyph are both SIGNED DISTANCE FIELDS: a number per pixel saying how
    far outside the shape you are, negative meaning inside. Once both are expressed that way,
    combining them is arithmetic rather than cases, and everything that would otherwise be a
    feature - antialiasing, outlines, glows - is a different threshold on the same number.

    The box's distance is computed analytically, so ITS CORNERS ARE EXACT at any size and any
    radius, with no geometry to regenerate when either changes. Only the glyph samples a stored
    field, so the usual "an SDF rounds off sharp corners" objection applies to text here and never
    to the buttons.

    --- WHY max(), AND WHY THAT REMOVES THE LAST BRANCH -------------------------------------------
    max() of two signed distances is INTERSECTION. That single operator covers both cases:

      - A GLYPH quad sets half_extent to its own cell and radius 0, so the box term is negative
        everywhere inside the quad and the glyph term decides. Where the box term wins - outside
        the cell - the glyph is clipped to its own cell, which is exactly what is wanted and is
        free rather than an extra test.

      - A RECT quad points its UVs at the font atlas's SOLID TEXEL, a texel baked to the maximum
        inside value. Its glyph term is therefore a large negative constant, the box term wins
        everywhere that matters, and the same code draws a plain rounded rectangle.

    So there is no mode flag, no uniform branch and no second shader. This is ImGui's white-pixel
    trick (one texture, one draw call, textured and untextured geometry together) carried over to
    distance fields.

    --- WHY THERE IS NO fwidth() -----------------------------------------------------------------
    Because every distance in here is already in PIXELS. The box term is built from pixel-valued
    attributes, and the glyph term is scaled into pixels on the CPU (see UIFontDistanceScale).
    So the antialiasing ramp is just `0.5 - d` clamped: one pixel wide, correct at any size, and
    needing no derivative instructions at all.

    That is worth more than the instruction it saves. Derivatives are a place desktop GL and GLES
    can differ in precision and in availability, and avoiding them is a large part of how this one
    stage manages to look the same on both. It also means the overlay antialiases ITSELF, so it is
    unaffected by the 16x MSAA the scene is drawn with here and by the port having no MSAA at all.
*/

uniform sampler2D atlas;

//Where the glyph edge sits in the sampled 0..1 range. From the baked font's header rather than
//hardcoded at 0.5, because it is a property of how the field was generated - see core/UIFont.h.
uniform float onedge;

in vec2 v_uv;
in vec2 v_local;

flat in vec2  v_half_extent;
flat in float v_radius;
flat in float v_outline;
flat in float v_distance_scale;
flat in vec4  v_color;

layout (location = 0) out vec4 out_color;

//Signed distance to a rounded box centred on the origin. Negative inside. The classic form: shrink
//the box by the radius, take the distance to that, then grow the result back by the radius.
float RoundBoxDistance(vec2 p, vec2 half_extent, float radius){
    vec2 q = abs(p) - half_extent + radius;
    return min(max(q.x,q.y),0.0) + length(max(q,vec2(0.0))) - radius;
}

void main(){
    float d_box   = RoundBoxDistance(v_local,v_half_extent,v_radius);
    float field   = texture(atlas,v_uv).r;
    float d_glyph = (onedge - field) * v_distance_scale;

    float d = max(d_box,d_glyph);

    //Outline without an if: mix SELECTS here, because step() is exactly 0 or 1. abs(d) - w is the
    //band of half-width w straddling the edge, which is the same shape the touch buttons draw by
    //hand today.
    d = mix(d,abs(d) - v_outline,step(0.001,v_outline));

    //One pixel of coverage across the edge. See the note above on why this needs no derivatives.
    float alpha = clamp(0.5 - d,0.0,1.0);

    //STRAIGHT alpha, not premultiplied, matching the GL_SRC_ALPHA/GL_ONE_MINUS_SRC_ALPHA blend
    //UIOverlay::Draw sets. Worth stating because this repo has a dormant layered-window path
    //(Window::CreateNewLayeredWindow, which nothing currently calls) whose UpdateLayeredWindow
    //uses AC_SRC_ALPHA and therefore wants PREMULTIPLIED pixels. If that path is ever revived,
    //this line and the blend func are the two places that have to change together.
    out_color = vec4(v_color.rgb,v_color.a * alpha);
}
