#include "han/world/water_surface.h"
#include "han/core/json.h"
#include <algorithm>
#include <cmath>

namespace han::world {
namespace {
using Polygon = std::vector<GeoVertex>;
double side(const GeoVertex& point, Vec2 a, Vec2 b) {
    return (double(b.x)-a.x)*(double(point.position[2])-a.y)-(double(b.y)-a.y)*(double(point.position[0])-a.x);
}
Polygon halfPlane(const Polygon& polygon, Vec2 a, Vec2 b, double sign) {
    Polygon result;
    if (polygon.empty()) return result;
    auto previous=polygon.back(); double before=side(previous,a,b)*sign;
    for (const auto& current : polygon) {
        const double after=side(current,a,b)*sign;
        if ((before>=0)!=(after>=0)) {
            const float t=static_cast<float>(before/(before-after));
            GeoVertex vertex{};
            for (int i=0;i<3;++i) {
                vertex.position[i]=previous.position[i]+t*(current.position[i]-previous.position[i]);
                vertex.normal[i]=previous.normal[i]+t*(current.normal[i]-previous.normal[i]);
                vertex.color[i]=previous.color[i]+t*(current.color[i]-previous.color[i]);
            }
            for (int i=0;i<2;++i) vertex.uv[i]=previous.uv[i]+t*(current.uv[i]-previous.uv[i]);
            result.push_back(vertex);
        }
        if (after>=0) result.push_back(current);
        previous=current; before=after;
    }
    return result;
}
double area(const Polygon& polygon) {
    double result=0;
    if (polygon.empty()) return result;
    const auto& origin=polygon[0].position;
    for (std::size_t i=1;i+1<polygon.size();++i) {
        const auto& a=polygon[i].position; const auto& b=polygon[i+1].position;
        result+=(double(a[0])-origin[0])*(double(b[2])-origin[2])-(double(a[2])-origin[2])*(double(b[0])-origin[0]);
    }
    return std::fabs(result)*.5;
}
// Выпуклая разность: внешние части разделены рёбрами водного треугольника.
bool subtract(const Polygon& polygon,const std::array<Vec2,3>& water,std::vector<Polygon>& output) {
    const double orientation=(double(water[1].x)-water[0].x)*(double(water[2].y)-water[0].y)-
                             (double(water[1].y)-water[0].y)*(double(water[2].x)-water[0].x);
    if (std::fabs(orientation)<1e-6) { output.push_back(polygon); return false; }
    const double sign=orientation>0 ? 1 : -1;
    Polygon intersection=polygon;
    for (int i=0;i<3;++i) intersection=halfPlane(intersection,water[i],water[(i+1)%3],sign);
    if (area(intersection)<1e-6) { output.push_back(polygon); return false; }
    Polygon inside=polygon;
    for (int i=0;i<3 && !inside.empty();++i) {
        auto outside=halfPlane(inside,water[i],water[(i+1)%3],-sign);
        if (area(outside)>1e-6) output.push_back(std::move(outside));
        inside=halfPlane(inside,water[i],water[(i+1)%3],sign);
    }
    return true;
}
}
bool WaterSurfaceMask::load(const std::string& path,std::string& error) {
    core::Json root;
    if (!core::loadJsonFile(path,root,error)) return false;
    const double cell=root["cell_size"].asNumber();
    if (root["version"].asNumber()!=1 || !root["cell_size"].isNumber() || !std::isfinite(cell) || cell<1 || cell>10000 ||
        !root["triangles"].isArray() || root["triangles"].size()>100000) { error="неверная маска воды"; return false; }
    WaterSurfaceMask result; result.cellSize_=static_cast<float>(cell);
    const CellGrid grid(result.cellSize_);
    for (const auto& item : root["triangles"].items()) {
        if (!item.isArray() || item.size()!=3) { error="неверный треугольник воды"; return false; }
        std::array<Vec2,3> triangle;
        for (std::size_t i=0;i<3;++i) {
            const auto& point=item.at(i);
            if (point.size()!=2 || !point.at(0).isNumber() || !point.at(1).isNumber() || !std::isfinite(point.at(0).asNumber()) || !std::isfinite(point.at(1).asNumber()) ||
                std::fabs(point.at(0).asNumber())>1000000 || std::fabs(point.at(1).asNumber())>1000000) { error="неверная вершина воды"; return false; }
            triangle[i]={static_cast<float>(point.at(0).asNumber()),static_cast<float>(point.at(1).asNumber())};
        }
        const auto low=grid.cellOf(std::min({triangle[0].x,triangle[1].x,triangle[2].x}),std::min({triangle[0].y,triangle[1].y,triangle[2].y}));
        const auto high=grid.cellOf(std::max({triangle[0].x,triangle[1].x,triangle[2].x}),std::max({triangle[0].y,triangle[1].y,triangle[2].y}));
        if (static_cast<double>(high.x-low.x+1)*(high.z-low.z+1)>100000) { error="слишком большой треугольник воды"; return false; }
        const auto index=result.triangles_.size(); result.triangles_.push_back(triangle);
        for (int x=low.x;x<=high.x;++x) for (int z=low.z;z<=high.z;++z) result.cells_[{x,z}].push_back(index);
    }
    *this=std::move(result); return true;
}

bool WaterSurfaceMask::covers(float x,float z) const {
    if (cellSize_<=0 || !std::isfinite(x) || !std::isfinite(z) || std::fabs(x)>1000000 || std::fabs(z)>1000000) return false;
    const auto found=cells_.find(CellGrid(cellSize_).cellOf(x,z));
    if (found==cells_.end()) return false;
    const auto cross=[&](Vec2 a,Vec2 b) { return (b.x-a.x)*(z-a.y)-(b.y-a.y)*(x-a.x); };
    for (const auto index : found->second) {
        const auto& p=triangles_[index];
        const float area=(p[1].x-p[0].x)*(p[2].y-p[0].y)-(p[1].y-p[0].y)*(p[2].x-p[0].x);
        if (std::fabs(area)<1e-6f) continue;
        const float a=cross(p[0],p[1]),b=cross(p[1],p[2]),c=cross(p[2],p[0]);
        if (!((a<0 || b<0 || c<0) && (a>0 || b>0 || c>0))) return true;
    }
    return false;
}

GeoMesh WaterSurfaceMask::removeCoveredTerrain(const GeoMesh& source) const {
    GeoMesh result; result.vertices=source.vertices;
    if (source.ranges.empty()) return source;
    for (const auto& range : source.ranges) {
        const auto first=static_cast<std::uint32_t>(result.indices.size());
        for (std::uint32_t index=range.firstIndex;index+2<range.firstIndex+range.indexCount;index+=3) {
            bool changed=false;
            std::vector<Polygon> pieces{{source.vertices[source.indices[index]],source.vertices[source.indices[index+1]],source.vertices[source.indices[index+2]]}};
            if (cellSize_>0 && (range.material==GeoMaterial::Terrain || range.material==GeoMaterial::Water)) {
                const auto& a=pieces[0][0].position; const auto& b=pieces[0][1].position; const auto& c=pieces[0][2].position;
                const CellGrid grid(cellSize_);
                const auto low=grid.cellOf(std::min({a[0],b[0],c[0]}),std::min({a[2],b[2],c[2]}));
                const auto high=grid.cellOf(std::max({a[0],b[0],c[0]}),std::max({a[2],b[2],c[2]}));
                std::vector<std::size_t> candidates;
                for (int x=low.x;x<=high.x;++x) for (int z=low.z;z<=high.z;++z) {
                    const auto found=cells_.find({x,z});
                    if (found!=cells_.end()) candidates.insert(candidates.end(),found->second.begin(),found->second.end());
                }
                std::sort(candidates.begin(),candidates.end()); candidates.erase(std::unique(candidates.begin(),candidates.end()),candidates.end());
                for (const auto candidate : candidates) {
                    std::vector<Polygon> remaining;
                    for (const auto& piece : pieces) changed=subtract(piece,triangles_[candidate],remaining) || changed;
                    pieces=std::move(remaining);
                    if (pieces.empty()) break;
                }
            }
            if (!changed) result.indices.insert(result.indices.end(),source.indices.begin()+index,source.indices.begin()+index+3);
            else for (const auto& polygon : pieces) {
                const auto base=static_cast<std::uint32_t>(result.vertices.size());
                result.vertices.insert(result.vertices.end(),polygon.begin(),polygon.end());
                for (std::uint32_t i=1;i+1<polygon.size();++i) result.indices.insert(result.indices.end(),{base,base+i,base+i+1});
            }
        }
        result.tag(range.material,first);
    }
    return result;
}
}
