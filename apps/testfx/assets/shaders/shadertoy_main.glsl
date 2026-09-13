/*
    The epilogue: the main() a pasted shadertoy snippet does not come with.

    Include it AFTER your mainImage, at the very bottom of the file, and include shaders/
    shadertoy.glsl at the top. Between the two goes the snippet, unchanged.

    fragCoord is derived from vuv rather than read from gl_FragCoord, and that is not a stylistic
    choice: gl_FragCoord is in WINDOW coordinates, while this renderer supports a restricted,
    offset viewport (Renderer::viewport_x/_y - Tank uses one). vuv * iResolution.xy is correct
    whatever the viewport is doing. This app never offsets its viewport, but an effect developed
    here is meant to be pasted into one that might, and the difference would show up there as an
    effect sliding away from the thing it is supposed to be on.

    frag_color is declared in shaders/shadertoy.glsl, so a native effect can use it too.
*/
void main(){
    mainImage(frag_color,vuv * iResolution.xy);
}
