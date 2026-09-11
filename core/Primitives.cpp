#include "Primitives.h"
#include "Debug.h"
#include "type_helpers.h"

#include <vector>
#include <math.h>

static Debugger* debug = new Debugger("Primitives",DEBUG_INFO);

//--- shared plumbing ---------------------------------------------------------------------------

static vertex MakeVertex(const vec3& pos,const vec3& normal,const vec3& tangent,const vec2& uv){
    vertex v = vertex();
    v.pos     = pos;
    v.normal  = normal;
    v.tangent = tangent;
    v.uv      = uv;
    v.matid   = 0;
    return v;
}

static void PushTri(std::vector<vertex>& out,const vertex& a,const vertex& b,const vertex& c){
    out.push_back(a);
    out.push_back(b);
    out.push_back(c);
}

//Corners in counter-clockwise order seen from OUTSIDE the solid. Split along a->c.
static void PushQuad(std::vector<vertex>& out,const vertex& a,const vertex& b,const vertex& c,const vertex& d){
    PushTri(out,a,b,c);
    PushTri(out,a,c,d);
}

static Mesh* FinishMesh(std::vector<vertex>& verts,const char* what){
    if (verts.empty()){
        debug->Err("%s: produced no vertices\n",what);
        return NULL;
    }
    Mesh* mesh = new Mesh();
    //SetMeshData computes the mesh's extents from these positions as it copies them in.
    mesh->SetMeshData(verts.data(),(int)verts.size());
    return mesh;
}

//--- box ---------------------------------------------------------------------------------------

//Per face: the outward normal, and two edge vectors whose cross product is that normal, so
//walking corner -> +u -> +u+v -> +v comes out counter-clockwise seen from outside. Lifted from
//ApplicationShip::BuildVolumeCube, which is the code this function exists to stop people writing.
static const vec3 kFaceNormal[6] = {
    vec3( 1, 0, 0), vec3(-1, 0, 0),
    vec3( 0, 1, 0), vec3( 0,-1, 0),
    vec3( 0, 0, 1), vec3( 0, 0,-1),
};
static const vec3 kFaceU[6] = {
    vec3( 0, 0,-1), vec3( 0, 0, 1),
    vec3( 1, 0, 0), vec3( 1, 0, 0),
    vec3( 1, 0, 0), vec3(-1, 0, 0),
};
static const vec3 kFaceV[6] = {
    vec3( 0, 1, 0), vec3( 0, 1, 0),
    vec3( 0, 0,-1), vec3( 0, 0, 1),
    vec3( 0, 1, 0), vec3( 0, 1, 0),
};

Mesh* MakeBox(const vec3& size){
    if (size.x <= 0.0f || size.y <= 0.0f || size.z <= 0.0f){
        debug->Err("MakeBox: every extent must be positive, got (%.3f,%.3f,%.3f)\n",size.x,size.y,size.z);
        return NULL;
    }

    std::vector<vertex>verts;
    verts.reserve(36); //6 faces * 2 triangles * 3

    for (int f = 0;f < 6;f++){
        vec3 n = kFaceNormal[f];
        vec3 u = kFaceU[f];
        vec3 v = kFaceV[f];

        //Built as a unit cube and then stretched per axis. Normals and tangents survive that
        //untouched because both are axis-aligned here - which is only true for a box, and is why
        //the curved primitives below take their size before computing normals instead.
        vec3 unit_corner = (n * 0.5f) - (u * 0.5f) - (v * 0.5f);
        vec3 unit_pos[4] = {unit_corner, unit_corner + u, unit_corner + u + v, unit_corner + v};
        vec2 uv[4] = {vec2(0,0), vec2(1,0), vec2(1,1), vec2(0,1)};

        vertex corner[4];
        for (int i = 0;i < 4;i++){
            vec3 p = vec3(unit_pos[i].x * size.x,unit_pos[i].y * size.y,unit_pos[i].z * size.z);
            corner[i] = MakeVertex(p,n,u,uv[i]);
        }
        PushQuad(verts,corner[0],corner[1],corner[2],corner[3]);
    }

    return FinishMesh(verts,"MakeBox");
}

//--- quad --------------------------------------------------------------------------------------

Mesh* MakeQuad(float width,float height){
    if (width <= 0.0f || height <= 0.0f){
        debug->Err("MakeQuad: width and height must be positive, got %.3f x %.3f\n",width,height);
        return NULL;
    }

    float hw = width * 0.5f;
    float hh = height * 0.5f;
    vec3 n = vec3(0,0,1);
    vec3 t = vec3(1,0,0);

    std::vector<vertex>verts;
    verts.reserve(6);

    //Counter-clockwise seen from +Z, i.e. from where the normal points.
    vertex a = MakeVertex(vec3(-hw,-hh,0),n,t,vec2(0,0));
    vertex b = MakeVertex(vec3( hw,-hh,0),n,t,vec2(1,0));
    vertex c = MakeVertex(vec3( hw, hh,0),n,t,vec2(1,1));
    vertex d = MakeVertex(vec3(-hw, hh,0),n,t,vec2(0,1));
    PushQuad(verts,a,b,c,d);

    return FinishMesh(verts,"MakeQuad");
}

//--- sphere ------------------------------------------------------------------------------------

//Ring i runs from the +Y pole (i == 0) to the -Y pole (i == rings); segment j runs once around.
static vertex SphereVertex(float radius,int i,int j,int rings,int segments){
    float phi   = TYPE_PI * (float)i / (float)rings;
    float theta = 2.0f * TYPE_PI * (float)j / (float)segments;
    float sp = sinf(phi);
    float cp = cosf(phi);
    float st = sinf(theta);
    float ct = cosf(theta);

    //Already unit length: (sp*ct)^2 + cp^2 + (sp*st)^2 == sp^2 + cp^2 == 1. So for a sphere the
    //normal IS the direction of the position, which is the whole reason its shading is smooth.
    vec3 n = vec3(sp * ct,cp,sp * st);

    //d/dtheta of the position, normalised - the sp factor cancels, so this stays valid at the
    //poles where the surface itself has no well-defined tangent direction.
    vec3 t = vec3(-st,0,ct);

    vec2 uv = vec2((float)j / (float)segments,1.0f - (float)i / (float)rings);
    return MakeVertex(n * radius,n,t,uv);
}

Mesh* MakeSphere(float radius,int segments,int rings){
    if (radius <= 0.0f){
        debug->Err("MakeSphere: radius must be positive, got %.3f\n",radius);
        return NULL;
    }
    if (segments < 3 || rings < 2){
        debug->Err("MakeSphere: need at least 3 segments and 2 rings, got %i and %i\n",segments,rings);
        return NULL;
    }

    std::vector<vertex>verts;
    verts.reserve((size_t)segments * rings * 6);

    for (int i = 0;i < rings;i++){
        for (int j = 0;j < segments;j++){
            vertex a = SphereVertex(radius,i,    j,    rings,segments);
            vertex b = SphereVertex(radius,i,    j + 1,rings,segments);
            vertex c = SphereVertex(radius,i + 1,j + 1,rings,segments);
            vertex d = SphereVertex(radius,i + 1,j,    rings,segments);

            //The pole rows are triangles, not quads: at i == 0 the whole i row sits on the +Y
            //pole so a and b are the same point, and at the bottom row c and d are. Emitting
            //the quad anyway would add a zero-area triangle per segment at each pole.
            if (i == 0){
                PushTri(verts,a,c,d);
            }else if (i == rings - 1){
                PushTri(verts,a,b,c);
            }else{
                PushQuad(verts,a,b,c,d);
            }
        }
    }

    return FinishMesh(verts,"MakeSphere");
}

//--- cylinder ----------------------------------------------------------------------------------

Mesh* MakeCylinder(float radius,float height,int segments,bool caps){
    if (radius <= 0.0f || height <= 0.0f){
        debug->Err("MakeCylinder: radius and height must be positive, got %.3f and %.3f\n",radius,height);
        return NULL;
    }
    if (segments < 3){
        debug->Err("MakeCylinder: need at least 3 segments, got %i\n",segments);
        return NULL;
    }

    float top = height * 0.5f;
    float bottom = -top;

    std::vector<vertex>verts;
    verts.reserve((size_t)segments * (caps ? 12 : 6));

    for (int j = 0;j < segments;j++){
        float theta0 = 2.0f * TYPE_PI * (float)j / (float)segments;
        float theta1 = 2.0f * TYPE_PI * (float)(j + 1) / (float)segments;
        float ct0 = cosf(theta0), st0 = sinf(theta0);
        float ct1 = cosf(theta1), st1 = sinf(theta1);

        vec3 n0 = vec3(ct0,0,st0);          //flank normal is purely radial
        vec3 n1 = vec3(ct1,0,st1);
        vec3 t0 = vec3(-st0,0,ct0);
        vec3 t1 = vec3(-st1,0,ct1);
        float u0 = (float)j / (float)segments;
        float u1 = (float)(j + 1) / (float)segments;

        //Flank. Upper edge first, matching the sphere's winding, which puts the outward normal
        //on the outside of the tube.
        vertex a = MakeVertex(vec3(ct0 * radius,top,   st0 * radius),n0,t0,vec2(u0,1));
        vertex b = MakeVertex(vec3(ct1 * radius,top,   st1 * radius),n1,t1,vec2(u1,1));
        vertex c = MakeVertex(vec3(ct1 * radius,bottom,st1 * radius),n1,t1,vec2(u1,0));
        vertex d = MakeVertex(vec3(ct0 * radius,bottom,st0 * radius),n0,t0,vec2(u0,0));
        PushQuad(verts,a,b,c,d);

        if (!caps){
            continue;
        }

        //Caps are triangle fans from the centre of each end, with their own flat normal so the
        //rim stays a hard edge rather than smearing into the flank's shading.
        vec3 cap_t = vec3(1,0,0);
        vec2 disc0 = vec2(0.5f + 0.5f * ct0,0.5f + 0.5f * st0);
        vec2 disc1 = vec2(0.5f + 0.5f * ct1,0.5f + 0.5f * st1);

        //Top (+Y). Reversed in azimuth relative to the bottom: seen from above, going the other
        //way round is what makes this counter-clockwise.
        vertex tc = MakeVertex(vec3(0,top,0),vec3(0,1,0),cap_t,vec2(0.5f,0.5f));
        vertex t1v = MakeVertex(vec3(ct1 * radius,top,st1 * radius),vec3(0,1,0),cap_t,disc1);
        vertex t0v = MakeVertex(vec3(ct0 * radius,top,st0 * radius),vec3(0,1,0),cap_t,disc0);
        PushTri(verts,tc,t1v,t0v);

        //Bottom (-Y).
        vertex bc = MakeVertex(vec3(0,bottom,0),vec3(0,-1,0),cap_t,vec2(0.5f,0.5f));
        vertex b0v = MakeVertex(vec3(ct0 * radius,bottom,st0 * radius),vec3(0,-1,0),cap_t,disc0);
        vertex b1v = MakeVertex(vec3(ct1 * radius,bottom,st1 * radius),vec3(0,-1,0),cap_t,disc1);
        PushTri(verts,bc,b0v,b1v);
    }

    return FinishMesh(verts,"MakeCylinder");
}

//--- cone --------------------------------------------------------------------------------------

Mesh* MakeCone(float radius,float height,int segments,bool cap){
    if (radius <= 0.0f || height <= 0.0f){
        debug->Err("MakeCone: radius and height must be positive, got %.3f and %.3f\n",radius,height);
        return NULL;
    }
    if (segments < 3){
        debug->Err("MakeCone: need at least 3 segments, got %i\n",segments);
        return NULL;
    }

    float top = height * 0.5f;
    float bottom = -top;

    //The flank's outward normal at azimuth theta is (height*cos, radius, height*sin) normalised
    //- from crossing the two surface derivatives. Both terms matter: drop the radius and a squat
    //cone shades like a tall one.
    auto FlankNormal = [radius,height](float ct,float st){
        vec3 n = vec3(height * ct,radius,height * st);
        n.normalize();
        return n;
    };

    std::vector<vertex>verts;
    verts.reserve((size_t)segments * (cap ? 6 : 3));

    for (int j = 0;j < segments;j++){
        float theta0 = 2.0f * TYPE_PI * (float)j / (float)segments;
        float theta1 = 2.0f * TYPE_PI * (float)(j + 1) / (float)segments;
        float thetam = (theta0 + theta1) * 0.5f;
        float ct0 = cosf(theta0), st0 = sinf(theta0);
        float ct1 = cosf(theta1), st1 = sinf(theta1);

        vec3 n0 = FlankNormal(ct0,st0);
        vec3 n1 = FlankNormal(ct1,st1);
        //The tip has no single normal, so this triangle's apex vertex gets the one from the
        //middle of its own segment. Sharing one averaged normal there instead is what makes a
        //cone tip look pinched.
        vec3 na = FlankNormal(cosf(thetam),sinf(thetam));

        vec3 t0 = vec3(-st0,0,ct0);
        vec3 t1 = vec3(-st1,0,ct1);
        vec3 tm = vec3(-sinf(thetam),0,cosf(thetam));
        float u0 = (float)j / (float)segments;
        float u1 = (float)(j + 1) / (float)segments;

        //Flank, wound the same way round as the cylinder's top cap.
        vertex apex = MakeVertex(vec3(0,top,0),na,tm,vec2((u0 + u1) * 0.5f,1));
        vertex e1 = MakeVertex(vec3(ct1 * radius,bottom,st1 * radius),n1,t1,vec2(u1,0));
        vertex e0 = MakeVertex(vec3(ct0 * radius,bottom,st0 * radius),n0,t0,vec2(u0,0));
        PushTri(verts,apex,e1,e0);

        if (!cap){
            continue;
        }

        vec3 cap_n = vec3(0,-1,0);
        vec3 cap_t = vec3(1,0,0);
        vertex bc = MakeVertex(vec3(0,bottom,0),cap_n,cap_t,vec2(0.5f,0.5f));
        vertex b0 = MakeVertex(vec3(ct0 * radius,bottom,st0 * radius),cap_n,cap_t,
                               vec2(0.5f + 0.5f * ct0,0.5f + 0.5f * st0));
        vertex b1 = MakeVertex(vec3(ct1 * radius,bottom,st1 * radius),cap_n,cap_t,
                               vec2(0.5f + 0.5f * ct1,0.5f + 0.5f * st1));
        PushTri(verts,bc,b0,b1);
    }

    return FinishMesh(verts,"MakeCone");
}
