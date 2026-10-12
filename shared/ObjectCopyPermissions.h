// Shared policy for the client affordance and authoritative server copy API.
// Copy rights deliberately do not inherit parcel writer/admin/editor rights.
#pragma once
#include "WorldObject.h"
#include "Parcel.h"
#include "UserID.h"
#include <cmath>

namespace ObjectCopyPermissions {

inline bool finiteBounds(const js::AABBox& bounds)
{
	for(int axis=0;axis<3;++axis)
		if(!std::isfinite(bounds.min_[axis]) || !std::isfinite(bounds.max_[axis]) || bounds.min_[axis]>bounds.max_[axis]) return false;
	return true;
}

// Parcel::AABBInParcel only tests its surrounding box. For a rotated parcel,
// test against all actual polygon half-planes, in either winding order.
inline bool parcelContainsBounds(const Parcel& parcel,const js::AABBox& bounds)
{
	if(parcel.state==Parcel::State_Dead || !finiteBounds(bounds) || !std::isfinite(parcel.zbounds.x) || !std::isfinite(parcel.zbounds.y) ||
		bounds.min_[2]<parcel.zbounds.x || bounds.max_[2]>parcel.zbounds.y) return false;
	double area=0;
	for(int i=0;i<4;++i) {
		const Vec2d& a=parcel.verts[i];const Vec2d& b=parcel.verts[(i+1)%4];
		if(!std::isfinite(a.x) || !std::isfinite(a.y)) return false;
		area+=a.x*b.y-a.y*b.x;
	}
	if(!std::isfinite(area) || std::abs(area)<1e-12) return false;
	const double winding=area>0?1:-1;
	for(int i=0;i<4;++i) {
		const Vec2d& a=parcel.verts[i];const Vec2d& b=parcel.verts[(i+1)%4];const Vec2d& c=parcel.verts[(i+2)%4];
		const double dx=b.x-a.x,dy=b.y-a.y;
		if(dx*dx+dy*dy<1e-20 || winding*(dx*(c.y-b.y)-dy*(c.x-b.x))<0) return false; // invalid/non-convex parcel
		for(int x=0;x<2;++x) for(int y=0;y<2;++y) {
			const double px=x?bounds.max_[0]:bounds.min_[0],py=y?bounds.max_[1]:bounds.min_[1];
			if(winding*(dx*(py-a.y)-dy*(px-a.x))<0) return false;
		}
	}
	return true;
}

template<class ParcelMap>
bool canPlaceCopy(const WorldObject& object,UserID user,UserID world_owner,const ParcelMap& parcels)
{
	if(!user.valid() || object.state==WorldObject::State_Dead || !finiteBounds(object.getAABBWS())) return false;
	if(isGodUser(user) || user==world_owner) return true;
	for(const auto& entry:parcels) {
		const Parcel& parcel=*entry.second;
		if(parcel.owner_id==user && parcelContainsBounds(parcel,object.getAABBWS())) return true;
	}
	return false;
}

template<class ParcelMap>
bool canCopyObject(const WorldObject& object,UserID user,UserID world_owner,const ParcelMap& parcels)
{
	if(!object.uid.valid() || !user.valid() || object.state==WorldObject::State_Dead) return false;
	if(!isGodUser(user) && object.creator_id!=user) return false;
	return canPlaceCopy(object,user,world_owner,parcels);
}

} // namespace ObjectCopyPermissions
