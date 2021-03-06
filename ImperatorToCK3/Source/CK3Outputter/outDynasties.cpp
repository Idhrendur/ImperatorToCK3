#include "outDynasties.h"
#include "CK3/Dynasties/Dynasty.h"
#include "CommonFunctions.h"
#include <filesystem>
#include <fstream>
#include <ranges>



void CK3::outputDynasties(const std::string& outputModName, const std::map<std::string, std::shared_ptr<Dynasty>>& dynasties) {
	const auto outputPath = "output/" + outputModName + "/common/dynasties/imp_dynasties.txt";
	std::ofstream output(outputPath); // dumping all into one file
	if (!output.is_open()) {
		throw std::runtime_error("Could not create dynasties file: " + outputPath);
	}
	output << commonItems::utf8BOM;
	for (const auto& dynasty : dynasties | std::views::values) {
		output << *dynasty;
	}
	output.close();
}
