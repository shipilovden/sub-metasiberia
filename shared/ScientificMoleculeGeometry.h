/*=====================================================================
ScientificMoleculeGeometry.h
----------------------------
Shared visual defaults for imported molecules and the scientific editor.
=====================================================================*/
#pragma once

#include <algorithm>
#include <array>
#include <string>

namespace ScientificMoleculeGeometry
{
	// Convert PubChem/SDF Angstrom coordinates into the engine's +Z-up
	// object space. Keep this identical for generated meshes and editor overlays.
	inline std::array<double, 3> sdfPositionToObjectSpace(double x, double y, double z,
		double center_x, double center_y, double center_z, double coordinate_scale)
	{
		const double centered_x = (x - center_x) * coordinate_scale;
		const double centered_y = (y - center_y) * coordinate_scale;
		const double centered_z = (z - center_z) * coordinate_scale;
		return {{centered_x, -centered_z, centered_y}};
	}

	inline double cpkRadiusScale(const std::string& element)
	{
		if(element == "H") return 0.58;
		if(element == "C") return 1.00;
		if(element == "N") return 0.95;
		if(element == "O") return 0.92;
		if(element == "S" || element == "P") return 1.10;
		return 0.96;
	}

	inline double atomBaseRadius(bool space_fill)
	{
		return space_fill ? 0.42 : 0.22;
	}

	inline double bondRadius(double thickness)
	{
		return std::max(0.015, std::min(0.5, 0.08 * (thickness / 0.10)));
	}

	inline double bondParallelSpacing(double radius)
	{
		return radius * 2.5;
	}

	inline double wedgeHalfWidth(double radius)
	{
		return radius * 3.0;
	}

	inline double wedgeHalfThickness(double radius)
	{
		return radius * 0.45;
	}

	inline double hashedBondRadius(double radius, double position)
	{
		return radius * (0.2 + 0.8 * position);
	}
}
