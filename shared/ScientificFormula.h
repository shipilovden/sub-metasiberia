/*=====================================================================
ScientificFormula.h
-------------------
Shared PubChem molecular formula parsing for server and Qt editor checks.
=====================================================================*/
#pragma once

#include "ScientificElementPalette.h"
#include <cstdlib>
#include <map>
#include <string>

namespace ScientificFormula
{
inline bool readMultiplier(const std::string& formula, size_t& pos, long& multiplier_out, bool& present_out)
{
	multiplier_out = 0;
	present_out = false;
	while(pos < formula.size() && formula[pos] >= '0' && formula[pos] <= '9')
	{
		present_out = true;
		const int digit = formula[pos++] - '0';
		// Formulas from PubChem are external data. Reject over-limit coefficients
		// before multiplying so a long digit run cannot overflow signed long.
		if(multiplier_out > (5000 - digit) / 10) return false;
		multiplier_out = multiplier_out * 10 + digit;
	}
	return true;
}

inline bool parseSequence(const std::string& formula, size_t& pos, char closing,
	int depth, std::map<std::string, int>& counts)
{
	if(depth > 8) return false;
	while(pos < formula.size())
	{
		const char c = formula[pos];
		if(closing != 0 && c == closing) return true;
		if(c == '.' || formula.compare(pos, 2, "\xC2\xB7") == 0) break;
		std::map<std::string, int> group_counts;
		if(c == '(' || c == '[')
		{
			const char group_close = c == '(' ? ')' : ']';
			++pos;
			if(!parseSequence(formula, pos, group_close, depth + 1, group_counts) ||
				pos >= formula.size() || formula[pos] != group_close || group_counts.empty())
				return false;
			++pos;
		}
		else
		{
			if(c < 'A' || c > 'Z') return false;
			std::string element(1, c);
			++pos;
			if(pos < formula.size() && formula[pos] >= 'a' && formula[pos] <= 'z')
				element.push_back(formula[pos++]);
			// PubChem may use D/T in formulas while the SDF uses element H and
			// an isotope mass number. They have the same elemental atom count.
			if(element == "D" || element == "T") element = "H";
			if(!ScientificElementPalette::isElementSymbol(element.c_str())) return false;
			group_counts[element] += 1;
		}
		long multiplier = 0;
		bool has_multiplier = false;
		if(!readMultiplier(formula, pos, multiplier, has_multiplier)) return false;
		if(!has_multiplier) multiplier = 1;
		if(multiplier < 1) return false;
		for(const auto& entry : group_counts)
		{
			const long total = (long)counts[entry.first] + (long)entry.second * multiplier;
			if(total > 5000) return false;
			counts[entry.first] = (int)total;
		}
	}
	return closing == 0;
}

inline bool parseCounts(const std::string& formula, std::map<std::string, int>& counts)
{
	counts.clear();
	if(formula.empty() || formula.size() > 1024) return false;
	size_t pos = 0;
	while(pos < formula.size())
	{
		long component_multiplier = 0;
		bool has_component_multiplier = false;
		if(!readMultiplier(formula, pos, component_multiplier, has_component_multiplier)) return false;
		if(!has_component_multiplier) component_multiplier = 1;
		if(component_multiplier < 1) return false;
		std::map<std::string, int> component_counts;
		if(!parseSequence(formula, pos, /*closing=*/0, /*depth=*/0, component_counts) || component_counts.empty())
			return false;
		for(const auto& entry : component_counts)
		{
			const long total = (long)counts[entry.first] + (long)entry.second * component_multiplier;
			if(total > 5000) return false;
			counts[entry.first] = (int)total;
		}
		if(pos == formula.size()) break;
		if(formula[pos] == '.') ++pos;
		else if(formula.compare(pos, 2, "\xC2\xB7") == 0) pos += 2;
		else return false;
		if(pos == formula.size()) return false;
	}
	return !counts.empty();
}

// PubChem's MolecularFormula includes the compound's net charge at the end,
// while MolecularFormulaNoCharge omits it. Some neutral PubChem records omit
// the redundant NoCharge field from their JSON row, so validate both forms
// when available and accept the signed suffix when that field is absent.
inline bool parsePubChemCounts(const std::string& molecular_formula, const std::string& molecular_formula_no_charge,
	int expected_charge, std::map<std::string, int>& counts)
{
	if(molecular_formula.empty() || molecular_formula.size() > 1024 || std::abs(expected_charge) > 40000)
		return false;
	std::string formula_body = molecular_formula;
	int formula_charge = 0;
	const size_t sign_pos = formula_body.find_last_of("+-");
	if(sign_pos != std::string::npos)
	{
		const int sign = formula_body[sign_pos] == '+' ? 1 : -1;
		int magnitude = 1;
		if(sign_pos + 1 < formula_body.size())
		{
			long parsed_magnitude = 0;
			for(size_t i = sign_pos + 1; i < formula_body.size(); ++i)
			{
				const char c = formula_body[i];
				if(c < '0' || c > '9' || parsed_magnitude > (40000 - (c - '0')) / 10)
					return false;
				parsed_magnitude = parsed_magnitude * 10 + (c - '0');
			}
			if(parsed_magnitude < 1 || parsed_magnitude > 40000) return false;
			magnitude = (int)parsed_magnitude;
		}
		formula_charge = sign * magnitude;
		formula_body.erase(sign_pos);
	}
	if(formula_charge != expected_charge) return false;
	std::map<std::string, int> formula_counts;
	if(!parseCounts(formula_body, formula_counts)) return false;
	if(!molecular_formula_no_charge.empty())
	{
		std::map<std::string, int> no_charge_counts;
		if(!parseCounts(molecular_formula_no_charge, no_charge_counts) || no_charge_counts != formula_counts)
			return false;
	}
	counts.swap(formula_counts);
	return true;
}
}
