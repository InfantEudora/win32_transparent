/*
    Lighting based off
    https://learnopengl.com/PBR/Lighting
    https://learnopengl.com/Lighting/Light-casters

    Kind of similar to this one: https://www.shadertoy.com/view/MdyGzR but not used as a reference.
*/

//Coordinates by which me sample the noise uv
float dx = 0.0;
float dy = 0.0;

//SimplexNoise settings
float octaves = 16.0;
float scale = 4.0;
float persistence = 0.6;

struct Light{
    int     shadow;
    vec3    position;   //Position if it's a point light, direction if its the sun
    vec3    direction;  //If the light is a spotlight
    vec3    color;
    float   brightness;
    float   cos_angle;    //If light is a spotlight
    bool    spotlight;
};

int num_active_lights; //Can be changed dynamically.
Light light[8]; //Array needs to be as bit as the max number of lights we intend to use

//Some hash function
vec3 hash33(vec3 p3){
	p3 = fract(p3 * vec3(0.1031, 0.11369, 0.13787));
    p3 += dot(p3, p3.yxz + 19.19);
    return -1.0 + 2.0 * fract(vec3((p3.x + p3.y) * p3.z, (p3.x + p3.z) * p3.y, (p3.y + p3.z) * p3.x));
}

// From https://www.shadertoy.com/view/MslBzf ->
//    Raw simplex implementation by candycat
//    Source: https://www.shadertoy.com/view/4sc3z2
float SimplexNoiseRaw(vec3 pos)
{
    const float K1 = 0.333333333;
    const float K2 = 0.166666667;

    vec3 i = floor(pos + (pos.x + pos.y + pos.z) * K1);
    vec3 d0 = pos - (i - (i.x + i.y + i.z) * K2);

    vec3 e = step(vec3(0.0), d0 - d0.yzx);
	vec3 i1 = e * (1.0 - e.zxy);
	vec3 i2 = 1.0 - e.zxy * (1.0 - e);

    vec3 d1 = d0 - (i1 - 1.0 * K2);
    vec3 d2 = d0 - (i2 - 2.0 * K2);
    vec3 d3 = d0 - (1.0 - 3.0 * K2);

    vec4 h = max(0.6 - vec4(dot(d0, d0), dot(d1, d1), dot(d2, d2), dot(d3, d3)), 0.0);
    vec4 n = h * h * h * h * vec4(dot(d0, hash33(i)), dot(d1, hash33(i + i1)), dot(d2, hash33(i + i2)), dot(d3, hash33(i + 1.0)));

    float d = dot(vec4(31.316), n);
    //Dot product will go to large negative values
    d = max(-0.150,d);
    return d;
}

float SimplexNoise(vec3  pos, float _octaves, float _scale, float _persistence){
    float final        = 0.0;
    float amplitude    = 1.0;
    float maxAmplitude = 0.0;

    for(float i = 0.0; i < _octaves; ++i){
        final        += SimplexNoiseRaw(pos * _scale) * amplitude;
        _scale        *= 2.0;
        maxAmplitude += amplitude;
        amplitude    *= _persistence;
    }
    return (final / maxAmplitude);
}


//Would normally pass scene info in via uniforms
void setup_scene(){
    num_active_lights = 3;
    light[0].color = vec3(1,0,0);
    light[0].position = vec3(0.5,0.5,0) + vec3(0.1*sin(iTime*1.2),0.1*cos(iTime),0);
    light[0].brightness = 0.01;
    light[1].color = vec3(0,1,0);
    light[1].position = vec3(0.7,0.5,0) + vec3(0.1*sin(iTime),0.1*cos(iTime*1.1),0);
    light[1].brightness = 0.01;


    light[2].color = vec3(0,0.2,1);
    light[2].position = vec3(1.5,0.5,0);
    light[2].direction = vec3(-1.0,0.0,0);
    light[2].spotlight = true;
    light[2].cos_angle = 0.95;
    light[2].brightness = 0.45;

    //Mouse control
    vec2 mo = iMouse.xy/iResolution.xy;
    mo.x *= iResolution.x / iResolution.y;

    light[2].direction.xy = mo - light[2].position.xy ;


    dx = iTime*0.01;
    dy = sin(iTime*0.01);
}

void mainImage( out vec4 fragColor, in vec2 fragCoord ){
    //Secene
    setup_scene();



    //Setup coordinates
    vec2 uv = fragCoord/iResolution.xy;
    uv.x *= iResolution.x / iResolution.y;
    vec2 uvin  = uv.xy + vec2(dx,dy)*3.0;

    //Generate clouds from simplexnoise
    vec3 val = vec3(0.15);
    val += vec3(SimplexNoise(vec3(uvin,iTime/100.0),octaves, scale, persistence));
    val += val; //Intensity
    val += vec3(SimplexNoise(vec3(uvin*2.0,iTime/50.0),octaves/4.0, scale*0.1, persistence));

    val /= 2.0;

    vec3 vposition = vec3(uv,0.0);

    //Calculate lights
    vec3 Lo = vec3(0.0); //Lo -> Light output
    for(int i = 0; i < num_active_lights; i++){
        float distance    = length(light[i].position - vposition);
        if (distance > 10.0 ){
            continue;
        }
        float intensity = 1.0;
        if (light[i].spotlight == true){
            //Light direction
            vec3 L = normalize(light[i].position - vposition);
            float theta = dot(L, normalize(-light[i].direction));
            if(theta < light[i].cos_angle){
                continue;
            }
            float epsilon = 0.15;
            intensity = clamp((theta - light[i].cos_angle) / epsilon, 0.0, 1.0);
        }

        float attenuation = 1.0 / (distance * distance);
        vec3 radiance     = light[i].color * light[i].brightness * attenuation * intensity;
         //Lo += (kD * albedo / PI + specular) * radiance * NdotL;
        Lo += radiance;


    }

    Lo = min(vec3(10.0,10.0,10.0),Lo);
    fragColor = vec4((val*Lo)+val,1.0);


}