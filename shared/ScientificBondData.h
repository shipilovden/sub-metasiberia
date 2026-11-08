/*=====================================================================
ScientificBondData.h
--------------------
Shared validation for chemical bond orders and SDF stereochemistry.
=====================================================================*/
#pragma once

namespace ScientificBondData
{
// In CTfile V2000, single bonds use 1=up, 4=either, 6=down; double bonds
// use 3 for unspecified cis/trans. V3000 CFG is normalized to those values.
inline bool isValidSDFStereo(int bond_order, int stereo)
{
	if(stereo == 0) return true;
	if(bond_order == 1) return stereo == 1 || stereo == 4 || stereo == 6;
	if(bond_order == 2) return stereo == 3;
	return false;
}
}
