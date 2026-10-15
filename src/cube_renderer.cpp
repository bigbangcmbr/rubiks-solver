#include "cube_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace rubiks {
namespace {

struct Point { float x, y, z; };
struct MeshPolygon {
    std::array<Point,20> corners{};
    std::array<Point,20> cornerNormals{};
    std::size_t cornerCount = 0;
    Point normal{};
    int owner = -1;
    int stickerIndex = -1;
};
struct FaceBasis { Point normal, right, down; };

constexpr float kPi = 3.14159265358979323846f;
constexpr float kShellHalf = 0.33f;
constexpr float kRoundRadius = 0.035f;
constexpr float kCore = kShellHalf-kRoundRadius;
constexpr Point kLightDirection{-0.45f, 0.80f, 0.55f};
constexpr float kCubieCenters[3]={-0.66f,0.0f,0.66f};
const FaceBasis kFaceBases[] = {
    {{0,1,0},{1,0,0},{0,0,1}}, {{1,0,0},{0,0,-1},{0,-1,0}}, {{0,0,1},{1,0,0},{0,-1,0}},
    {{0,-1,0},{1,0,0},{0,0,-1}}, {{-1,0,0},{0,0,1},{0,-1,0}}, {{0,0,-1},{-1,0,0},{0,-1,0}}
};

Point add(Point a, Point b) { return {a.x+b.x,a.y+b.y,a.z+b.z}; }
Point scale(Point p,float s) { return {p.x*s,p.y*s,p.z*s}; }
Point normalize(Point p) {
    const float length=std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z);
    return length>0 ? scale(p,1.0f/length) : Point{0,0,1};
}
Point rotate(Point p,float yaw,float pitch) {
    const float cy=std::cos(yaw),sy=std::sin(yaw),cp=std::cos(pitch),sp=std::sin(pitch);
    Point y{cy*p.x+sy*p.z,p.y,-sy*p.x+cy*p.z};
    return {y.x,cp*y.y-sp*y.z,sp*y.y+cp*y.z};
}
std::array<float,3> sticker_rgb(char c) {
    std::array<float,3> color{};
    switch(c) {
    case 'U': color={0.94f,0.94f,0.92f}; break;
    case 'R': color={0.82f,0.08f,0.10f}; break;
    case 'F': color={0.04f,0.62f,0.24f}; break;
    case 'D': color={0.98f,0.78f,0.04f}; break;
    case 'L': color={1.00f,0.36f,0.03f}; break;
    default: color={0.03f,0.30f,0.82f}; break;
    }
    return color;
}
Point cubie_center(int ix,int iy,int iz) {
    return {kCubieCenters[ix],kCubieCenters[iy],kCubieCenters[iz]};
}
std::vector<MeshPolygon> make_mesh() {
    std::vector<MeshPolygon> mesh;
    mesh.reserve(kMaxCubeVertices/6);
    auto add_polygon=[&](const Point *corners,std::size_t count,Point normal,int owner,int stickerIndex=-1,
                         const Point *cornerNormals=nullptr) {
        MeshPolygon polygon{};
        polygon.cornerCount=count;
        std::copy_n(corners,count,polygon.corners.begin());
        for(std::size_t i=0;i<count;++i) polygon.cornerNormals[i]=cornerNormals?cornerNormals[i]:normal;
        polygon.normal=normal; polygon.owner=owner; polygon.stickerIndex=stickerIndex;
        mesh.push_back(polygon);
    };
    auto add_face_polygon=[&](Point center,const FaceBasis &basis,float half,Point normal,int owner,
                             int stickerIndex=-1,float radius=0.0f) {
        std::array<Point,20> corners{};
        std::size_t count=4;
        if(radius<=0.0f) {
            corners={add(add(center,scale(basis.right,-half)),scale(basis.down,-half)),
                     add(add(center,scale(basis.right, half)),scale(basis.down,-half)),
                     add(add(center,scale(basis.right, half)),scale(basis.down, half)),
                     add(add(center,scale(basis.right,-half)),scale(basis.down, half))};
        } else {
            const float inner=half-radius;
            const std::array<std::array<float,3>,4> arcs{{
                {{-inner,-inner,kPi}},{{inner,-inner,1.5f*kPi}},
                {{inner,inner,0.0f}},{{-inner,inner,0.5f*kPi}}
            }};
            count=0;
            for(const auto &arc:arcs) for(int segment=0;segment<=4;++segment) {
                const float angle=arc[2]+0.5f*kPi*segment/4.0f;
                corners[count++]=add(add(center,scale(basis.right,arc[0]+radius*std::cos(angle))),
                                            scale(basis.down,arc[1]+radius*std::sin(angle)));
            }
        }
        add_polygon(corners.data(),count,normal,owner,stickerIndex);
    };
    auto cubie_point=[](Point center,const std::array<float,3> &p) {
        return Point{center.x+p[0],center.y+p[1],center.z+p[2]};
    };
    for(int ix=0;ix<3;++ix) for(int iy=0;iy<3;++iy) for(int iz=0;iz<3;++iz) {
        const int owner=ix*9+iy*3+iz;
        const Point cubie{ kCubieCenters[ix],kCubieCenters[iy],kCubieCenters[iz] };
        for(const auto &basis:kFaceBases) {
            const Point faceCenter=add(cubie,scale(basis.normal,kShellHalf));
            add_face_polygon(faceCenter,basis,kCore,basis.normal,owner);
        }
        for(int axisA=0;axisA<3;++axisA) for(int axisB=axisA+1;axisB<3;++axisB) {
            const int axisC=3-axisA-axisB;
            for(int signA:{-1,1}) for(int signB:{-1,1}) for(int segment=0;segment<6;++segment) {
                const float a0=0.5f*kPi*segment/6.0f,a1=0.5f*kPi*(segment+1)/6.0f,am=0.5f*(a0+a1);
                auto edgePoint=[&](float angle,float along) {
                    std::array<float,3> p{};
                    p[axisA]=signA*(kCore+kRoundRadius*std::cos(angle));
                    p[axisB]=signB*(kCore+kRoundRadius*std::sin(angle)); p[axisC]=along;
                    return cubie_point(cubie,p);
                };
                auto edgeNormal=[&](float angle) {
                    std::array<float,3> nc{}; nc[axisA]=signA*std::cos(angle); nc[axisB]=signB*std::sin(angle);
                    return Point{nc[0],nc[1],nc[2]};
                };
                const Point normal=edgeNormal(am);
                const std::array<Point,4> corners{edgePoint(a0,-kCore),edgePoint(a0,kCore),
                                                   edgePoint(a1,kCore),edgePoint(a1,-kCore)};
                const std::array<Point,4> normals{edgeNormal(a0),edgeNormal(a0),edgeNormal(a1),edgeNormal(a1)};
                add_polygon(corners.data(),corners.size(),normal,owner,-1,normals.data());
            }
        }
        for(int sx:{-1,1}) for(int sy:{-1,1}) for(int sz:{-1,1}) {
            auto cornerPoint=[&](float theta,float phi) {
                return Point{cubie.x+sx*(kCore+kRoundRadius*std::sin(theta)*std::cos(phi)),
                             cubie.y+sy*(kCore+kRoundRadius*std::sin(theta)*std::sin(phi)),
                             cubie.z+sz*(kCore+kRoundRadius*std::cos(theta))};
            };
            for(int ti=0;ti<6;++ti) for(int pi=0;pi<6;++pi) {
                const float t0=0.5f*kPi*ti/6.0f,t1=0.5f*kPi*(ti+1)/6.0f;
                const float p0=0.5f*kPi*pi/6.0f,p1=0.5f*kPi*(pi+1)/6.0f;
                const float tm=0.5f*(t0+t1),pm=0.5f*(p0+p1);
                const Point normal{sx*std::sin(tm)*std::cos(pm),sy*std::sin(tm)*std::sin(pm),sz*std::cos(tm)};
                const std::array<Point,4> corners{cornerPoint(t0,p0),cornerPoint(t0,p1),
                                                   cornerPoint(t1,p1),cornerPoint(t1,p0)};
                const auto cornerNormal=[&](float theta,float phi) {
                    return Point{sx*std::sin(theta)*std::cos(phi),sy*std::sin(theta)*std::sin(phi),sz*std::cos(theta)};
                };
                const std::array<Point,4> normals{cornerNormal(t0,p0),cornerNormal(t0,p1),
                                                   cornerNormal(t1,p1),cornerNormal(t1,p0)};
                add_polygon(corners.data(),corners.size(),normal,owner,-1,normals.data());
            }
        }
        for(int face=0;face<6;++face) {
            const bool outer=(face==0&&iy==2)||(face==1&&ix==2)||(face==2&&iz==2)||
                             (face==3&&iy==0)||(face==4&&ix==0)||(face==5&&iz==0);
            if(!outer) continue;
            const int row=face==0?iz:face==3?2-iz:2-iy;
            const int col=face==1?2-iz:face==4?iz:(face==5?2-ix:ix);
            const int sticker=face*9+row*3+col;
            const auto &basis=kFaceBases[face];
            // This is a material region of the face mesh, offset only to avoid a depth tie.
            const Point stickerCenter=add(cubie,scale(basis.normal,kShellHalf+0.0005f));
            add_face_polygon(stickerCenter,basis,0.285f,basis.normal,owner,sticker,0.04f);
        }
    }
    return mesh;
}

const std::vector<MeshPolygon> &mesh() {
    static const std::vector<MeshPolygon> value=make_mesh();
    return value;
}

} // namespace

std::size_t build_cube_vertices(const Cube &cube,float yaw,float pitch,float cameraDistance,
                               std::uint32_t width,std::uint32_t height,RenderVertex *vertices,
                               std::size_t capacity) {
    const float aspectFix=static_cast<float>(height)/static_cast<float>(width);
    const auto project=[&](Point p) {
        const Point view=rotate(p,yaw,pitch);
        const float nearPlane=0.1f,farPlane=100.0f;
        const float distance=std::max(1.2f,cameraDistance-view.z);
        const float scale=2.7f/distance;
        const float depth=farPlane/(farPlane-nearPlane)-(farPlane*nearPlane)/((farPlane-nearPlane)*distance);
        return std::array<float,3>{view.x*scale*aspectFix,-view.y*scale,depth};
    };
    std::size_t written=0;
    for(const auto &polygon:mesh()) {
        const Point normalCamera=normalize(rotate(polygon.normal,yaw,pitch));
        if(normalCamera.z<=0.001f) continue;
        std::array<float,3> color{};
        if(polygon.stickerIndex>=0) {
            color=sticker_rgb(cube.stickers[polygon.stickerIndex]);
            const Point n=normalize(rotate(polygon.normal,yaw,pitch));
            const float light=0.72f+0.28f*std::max(0.0f,n.x*0.35f+n.y*0.65f+n.z*0.68f);
            for(float &channel:color) channel*=light;
        } else {
            const Point n=polygon.normal;
            const float light=0.58f+0.42f*std::max(0.0f,n.x*0.35f+n.y*0.65f+n.z*0.68f);
            color={0.075f*light,0.078f*light,0.084f*light};
        }
        for(std::size_t triangle=1;triangle+1<polygon.cornerCount;++triangle) {
            const std::size_t indices[3]={0,triangle,triangle+1};
            for(std::size_t k=0;k<3;++k) {
                if(written>=capacity) throw std::runtime_error("Cube renderer vertex capacity exceeded");
                const std::size_t index=indices[k];
                const auto position=project(polygon.corners[index]);
                vertices[written++]={position[0],position[1],position[2],color[0],color[1],color[2]};
            }
        }
    }
    return written;
}

} // namespace rubiks
