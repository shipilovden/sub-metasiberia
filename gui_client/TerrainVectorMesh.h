#pragma once

// Tessellate original vector strokes against the actual rendered heightfield
// triangles, rather than sampling a low-resolution colour mask. CPU worker only.
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace TerrainVectorMesh
{
struct Point { double x, y, z; };
struct Vertex { float x,y,z,nx,ny,nz; };
struct Polygon { std::array<Point, 20> p; int n = 0; };
inline double side(Point a, Point b, Point p) { return (b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x); }
inline Polygon clip(const Polygon& in, Point a, Point b)
{
	Polygon out;
	if(in.n == 0) return out;
	Point prev = in.p[in.n-1]; double pd = side(a,b,prev);
	for(int i=0;i<in.n;++i)
	{
		const Point cur=in.p[i]; const double cd=side(a,b,cur);
		if((pd>=0)!=(cd>=0))
		{
			const double t=pd/(pd-cd);
			if(out.n >= (int)out.p.size()) throw std::runtime_error("Terrain vector polygon exceeds clipping budget");
			out.p[out.n++]={prev.x+t*(cur.x-prev.x),prev.y+t*(cur.y-prev.y),0};
		}
		if(cd>=0)
		{
			if(out.n >= (int)out.p.size()) throw std::runtime_error("Terrain vector polygon exceeds clipping budget");
			out.p[out.n++]=cur;
		}
		prev=cur; pd=cd;
	}
	return out;
}

class Builder
{
public:
	Builder(const unsigned char* vertices, size_t stride, int padded_res, double width, const std::atomic<bool>& cancel)
	: data(vertices), stride(stride), res(padded_res), cells(padded_res-3), step(width/cells), cancelled(cancel)
	{ bounds_min={0,0,0}; bounds_max={width,width,0}; }
	std::array<std::vector<Vertex>,6> layers;
	Point bounds_min, bounds_max;

	// Round caps also close joints between consecutive original OSM edges.
	void stroke(Point a, Point b, double width, int layer, double lift)
	{
		const double dx=b.x-a.x,dy=b.y-a.y,length=std::hypot(dx,dy),r=width*0.5;
		if(length<1.e-8 || std::max(a.x,b.x)+r<bounds_min.x || std::min(a.x,b.x)-r>bounds_max.x ||
			std::max(a.y,b.y)+r<bounds_min.y || std::min(a.y,b.y)-r>bounds_max.y) return;
		// Clip the centreline to the chunk expanded by its radius before
		// subdividing: a long OSM edge must not create work outside this chunk.
		double lo=0,hi=1;
		auto slab=[&](double p,double d,double mn,double mx) {
			if(std::abs(d)<1.e-12) return p>=mn && p<=mx;
			double t0=(mn-p)/d,t1=(mx-p)/d; if(t0>t1) std::swap(t0,t1);
			lo=std::max(lo,t0); hi=std::min(hi,t1); return lo<=hi;
		};
		if(!slab(a.x,dx,bounds_min.x-r,bounds_max.x+r) || !slab(a.y,dy,bounds_min.y-r,bounds_max.y+r)) return;
		const double nx=-dy/length*r,ny=dx/length*r;
		const int count=std::max(1,(int)std::ceil(length*(hi-lo)/(2*step)));
		for(int i=0;i<count && !cancelled.load(std::memory_order_relaxed);++i)
		{
			const double t0=lo+(hi-lo)*i/count,t1=lo+(hi-lo)*(i+1)/count;
			const Point p={a.x+dx*t0,a.y+dy*t0,0},q={a.x+dx*t1,a.y+dy*t1,0};
			Polygon poly; poly.n=4;
			poly.p[0]={p.x-nx,p.y-ny,0}; poly.p[1]={q.x-nx,q.y-ny,0};
			poly.p[2]={q.x+nx,q.y+ny,0}; poly.p[3]={p.x+nx,p.y+ny,0};
			tessellate(poly,layer,lift);
		}
		for(Point p : {a,b})
		{
			if(p.x+r<bounds_min.x || p.x-r>bounds_max.x || p.y+r<bounds_min.y || p.y-r>bounds_max.y) continue;
			Polygon disk; disk.n=12;
			for(int i=0;i<12;++i) { const double angle=i*6.283185307179586/12; disk.p[i]={p.x+r*std::cos(angle),p.y+r*std::sin(angle),0}; }
			tessellate(disk,layer,lift);
		}
	}
private:
	Point vertex(int x,int y) const
	{
		const float* p=reinterpret_cast<const float*>(data+((y+1)*res+x+1)*stride);
		return {p[0],p[1],p[2]};
	}
	void tessellate(Polygon poly,int layer,double lift)
	{
		const Point corners[4]={{bounds_min.x,bounds_min.y,0},{bounds_max.x,bounds_min.y,0},
			{bounds_max.x,bounds_max.y,0},{bounds_min.x,bounds_max.y,0}};
		for(int i=0;i<4;++i) poly=clip(poly,corners[i],corners[(i+1)%4]);
		if(poly.n<3) return;
		double minx=poly.p[0].x,maxx=minx,miny=poly.p[0].y,maxy=miny;
		for(int i=1;i<poly.n;++i) { minx=std::min(minx,poly.p[i].x);maxx=std::max(maxx,poly.p[i].x);miny=std::min(miny,poly.p[i].y);maxy=std::max(maxy,poly.p[i].y); }
		const int x0=std::clamp((int)std::floor(minx/step),0,cells-1),x1=std::clamp((int)std::floor(maxx/step),0,cells-1);
		const int y0=std::clamp((int)std::floor(miny/step),0,cells-1),y1=std::clamp((int)std::floor(maxy/step),0,cells-1);
		for(int y=y0;y<=y1;++y)
		{
			if(cancelled.load(std::memory_order_relaxed)) return;
			for(int x=x0;x<=x1;++x)
			{
				const Point a=vertex(x,y),b=vertex(x+1,y),c=vertex(x+1,y+1),d=vertex(x,y+1);
				triangle(poly,a,b,d,layer,lift); triangle(poly,b,c,d,layer,lift);
			}
		}
	}
	void triangle(Polygon poly,Point a,Point b,Point c,int layer,double lift)
	{
		poly=clip(poly,a,b); poly=clip(poly,b,c); poly=clip(poly,c,a);
		if(poly.n<3) return;
		const double area=side(a,b,c);
		if(area<=1.e-15) return;
		const double nx=(b.y-a.y)*(c.z-a.z)-(b.z-a.z)*(c.y-a.y);
		const double ny=(b.z-a.z)*(c.x-a.x)-(b.x-a.x)*(c.z-a.z);
		const double norm=std::sqrt(nx*nx+ny*ny+area*area);
		auto v=[&](Point p) {
			const double z=(side(b,c,p)*a.z+side(c,a,p)*b.z+side(a,b,p)*c.z)/area;
			return Vertex{(float)p.x,(float)p.y,(float)(z+lift),(float)(nx/norm),(float)(ny/norm),(float)(area/norm)};
		};
		for(int i=1;i+1<poly.n;++i)
		{
			if(side(poly.p[0],poly.p[i],poly.p[i+1])<1.e-12) continue;
			// Bounded allocation on corrupt/unusually dense input. Existing chunk
			// failure handling retains visible geometry and reports the failure.
			if(total_vertices+3>2000000) throw std::runtime_error("Terrain vector chunk exceeds geometry budget");
			layers[layer].push_back(v(poly.p[0]));layers[layer].push_back(v(poly.p[i]));layers[layer].push_back(v(poly.p[i+1]));
			total_vertices+=3;
		}
	}
	const unsigned char* data; size_t stride; int res,cells; double step;
	const std::atomic<bool>& cancelled; size_t total_vertices=0;
};
}
