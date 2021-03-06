#include "CK3World.h"
#include "Imperator/ImperatorWorld.h"
#include "Imperator/Characters/Character.h"
#include "Imperator/Countries/Country.h"
#include "Imperator/Provinces/Province.h"
#include "Configuration/Configuration.h"
#include "Province/CK3Provinces.h"
#include "Province/CK3ProvinceMappings.h"
#include "Titles/Title.h"
#include "Log.h"
#include "OSCompatibilityLayer.h"
#include "ConverterVersion.h"
#include <filesystem>
#include <ranges>
#include <cmath>



using std::string;
using std::pair;
using std::map;
using std::set;
using std::vector;
using std::make_shared;
using std::shared_ptr;
using std::optional;


CK3::World::World(const Imperator::World& impWorld, const Configuration& theConfiguration, const commonItems::ConverterVersion& converterVersion) {
	Log(LogLevel::Info) << "*** Hello CK3, let's get painting. ***";
	// Scraping localizations from Imperator so we may know proper names for our countries.
	localizationMapper.scrapeLocalizations(theConfiguration, impWorld.getMods());

	// Loading Imperator CoAs to use them for generated CK3 titles
	coaMapper = mappers::CoaMapper(theConfiguration);

	// Load vanilla titles history
	titlesHistory = TitlesHistory(theConfiguration.getCK3Path() + "/game/history/titles");

	// Loading vanilla CK3 landed titles
	landedTitles.loadTitles(theConfiguration.getCK3Path() + "/game/common/landed_titles/00_landed_titles.txt");
	addHistoryToVanillaTitles();

	// Loading regions
	ck3RegionMapper = make_shared<mappers::CK3RegionMapper>(theConfiguration.getCK3Path(), landedTitles);
	imperatorRegionMapper = make_shared<mappers::ImperatorRegionMapper>(theConfiguration.getImperatorPath());
	// Use the region mappers in other mappers
	religionMapper.loadRegionMappers(imperatorRegionMapper, ck3RegionMapper);
	cultureMapper.loadRegionMappers(imperatorRegionMapper, ck3RegionMapper);

	importImperatorCountries(impWorld.getCountries());

	// Now we can deal with provinces since we know to whom to assign them. We first import vanilla province data.
	// Some of it will be overwritten, but not all.
	importVanillaProvinces(theConfiguration.getCK3Path());

	// Next we import Imperator provinces and translate them ontop a significant part of all imported provinces.
	importImperatorProvinces(impWorld);


	importImperatorCharacters(impWorld, theConfiguration.getConvertBirthAndDeathDates(), impWorld.getEndDate());
	linkSpouses();
	linkMothersAndFathers();

	importImperatorFamilies(impWorld);


	overWriteCountiesHistory();
	removeInvalidLandlessTitles();

	purgeLandlessVanillaCharacters();
}


void CK3::World::importImperatorCharacters(const Imperator::World& impWorld, const bool ConvertBirthAndDeathDates = true, const date endDate = date(867, 1, 1)) {
	Log(LogLevel::Info) << "-> Importing Imperator Characters";

	for (const auto& character : impWorld.getCharacters()) {
		importImperatorCharacter(character, ConvertBirthAndDeathDates, endDate);
	}
	Log(LogLevel::Info) << ">> " << characters.size() << " total characters recognized.";
}


void CK3::World::importImperatorCharacter(const pair<unsigned long long, shared_ptr<Imperator::Character>>& character,
										  const bool ConvertBirthAndDeathDates = true,
										  const date endDate = date(867, 1, 1)) {
	// Create a new CK3 character
	auto newCharacter = make_shared<Character>();
	newCharacter->initializeFromImperator(character.second,
										  religionMapper,
										  cultureMapper,
										  traitMapper,
										  nicknameMapper,
										  localizationMapper,
										  provinceMapper,
										  deathReasonMapper,
										  ConvertBirthAndDeathDates,
										  endDate);
	character.second->registerCK3Character(newCharacter);
	characters.emplace(newCharacter->ID, newCharacter);
}


void CK3::World::importImperatorCountries(const map<unsigned long long, shared_ptr<Imperator::Country>>& imperatorCountries) {
	Log(LogLevel::Info) << "-> Importing Imperator Countries";

	// landedTitles holds all titles imported from CK3. We'll now overwrite some and
	// add new ones from Imperator tags.
	for (const auto& title : imperatorCountries) {
		importImperatorCountry(title, imperatorCountries);
	}
	Log(LogLevel::Info) << ">> " << getTitles().size() << " total countries recognized.";
}


void CK3::World::importImperatorCountry(const pair<unsigned long long, shared_ptr<Imperator::Country>>& country, 
										const map<unsigned long long, shared_ptr<Imperator::Country>>& imperatorCountries) {
	// Create a new title
	auto newTitle = make_shared<Title>();
	newTitle->initializeFromTag(country.second,
								imperatorCountries,
								localizationMapper,
								landedTitles,
								provinceMapper,
								coaMapper,
								tagTitleMapper,
								governmentMapper,
								successionLawMapper);
	
	const auto& name = newTitle->getName();
	if (const auto titleItr = getTitles().find(name); titleItr != getTitles().end()) {
		const auto& vanillaTitle = titleItr->second;
		vanillaTitle->updateFromTitle(newTitle);
		country.second->setCK3Title(vanillaTitle);
	}
	else {
		landedTitles.insertTitle(newTitle);
		country.second->setCK3Title(newTitle);
	}
}


void CK3::World::importVanillaProvinces(const string& ck3Path) {
	Log(LogLevel::Info) << "-> Importing Vanilla Provinces";
	// ---- Loading history/provinces
	auto fileNames = commonItems::GetAllFilesInFolderRecursive(ck3Path + "/game/history/provinces");
	for (const auto& fileName : fileNames) {
		if (!fileName.ends_with(".txt"))
			continue;
		try {
			auto newProvinces = Provinces(ck3Path + "/game/history/provinces/" + fileName);
			for (const auto& [newProvinceID, newProvince] : newProvinces.getProvinces()) {
				if (auto provinceItr = provinces.find(newProvinceID); provinceItr != provinces.end()) {
					Log(LogLevel::Warning) << "Vanilla province duplication - " << newProvinceID << " already loaded! Overwriting.";
					provinceItr->second = newProvince;
				}
				else {
					provinces.emplace(newProvinceID, newProvince);
				}
			}
		}
		catch (std::exception& e) {
			Log(LogLevel::Warning) << "Invalid province filename: " << ck3Path << "/game/history/provinces/" << fileName << " : " << e.what();
		}
	}

	// now load the provinces that don't have unique entries in history/provinces
	// they instead use history/province_mapping
	fileNames = commonItems::GetAllFilesInFolderRecursive(ck3Path + "/game/history/province_mapping");
	for (const auto& fileName : fileNames) {
		if (!fileName.ends_with(".txt"))
			continue;
		try {
			auto newProvinces = ProvinceMappings(ck3Path + "/game/history/province_mapping/" + fileName);
			for (const auto& [newProvinceID, baseProvinceID] : newProvinces.getMappings()) {
				if (!provinces.contains(baseProvinceID)) {
					Log(LogLevel::Warning) << "Base province " << baseProvinceID << " not found for province " << newProvinceID << ".";
					continue;
				}
				if (provinces.contains(newProvinceID)) {
					Log(LogLevel::Info) << "Vanilla province duplication - " << newProvinceID << " already loaded! Preferring unique entry over mapping.";
				}
				else {
					auto newProvince = make_shared<Province>(newProvinceID, *provinces.find(baseProvinceID)->second);
					provinces.emplace(newProvinceID, newProvince);
				}
			}
		}
		catch (std::exception& e) {
			Log(LogLevel::Warning) << "Invalid province filename: " << ck3Path << "/game/history/province_mapping/" << fileName << " : " << e.what();
		}
	}


	Log(LogLevel::Info) << ">> Loaded " << provinces.size() << " province definitions.";
}


void CK3::World::importImperatorProvinces(const Imperator::World& impWorld) {
	Log(LogLevel::Info) << "-> Importing Imperator Provinces";
	auto counter = 0;
	// Imperator provinces map to a subset of CK3 provinces. We'll only rewrite those we are responsible for.
	for (const auto& [provinceID, province] : provinces) {
		const auto& impProvinces = provinceMapper.getImperatorProvinceNumbers(provinceID);
		// Provinces we're not affecting will not be in this list.
		if (impProvinces.empty())
			continue;
		// Next, we find what province to use as its initializing source.
		const auto& sourceProvince = determineProvinceSource(impProvinces, impWorld);
		if (!sourceProvince) {
			Log(LogLevel::Warning) << "Could not determine source province for CK3 province " << provinceID;
			continue; // MISMAP, or simply have mod provinces loaded we're not using.
		}
		else {
			province->initializeFromImperator(sourceProvince->second, cultureMapper, religionMapper);
		}
		// And finally, initialize it.
		++counter;
	}
	Log(LogLevel::Info) << ">> " << impWorld.getProvinces().size() << " Imperator provinces imported into " << counter << " CK3 provinces.";
}


optional<pair<unsigned long long, shared_ptr<Imperator::Province>>> CK3::World::determineProvinceSource(const vector<unsigned long long>& impProvinceNumbers,
	const Imperator::World& impWorld) const
{
	// determine ownership by province development.
	map<unsigned long long, vector<shared_ptr<Imperator::Province>>> theClaims; // owner, offered province sources
	map<unsigned long long, int> theShares;														// owner, development
	optional<unsigned long long> winner;
	auto maxDev = -1;

	for (auto imperatorProvinceID : impProvinceNumbers) {
		const auto& impProvince = impWorld.getProvinces().find(imperatorProvinceID);
		if (impProvince == impWorld.getProvinces().end()) {
			Log(LogLevel::Warning) << "Source province " << imperatorProvinceID << " is not on the list of known provinces!";
			continue; // Broken mapping, or loaded a mod changing provinces without using it.
		}
		const auto ownerID = impProvince->second->getOwner().first;
		theClaims[ownerID].emplace_back(impProvince->second);
		theShares[ownerID] = lround(impProvince->second->getBuildingsCount() + impProvince->second->getPopCount());
	}
	// Let's see who the lucky winner is.
	for (const auto& [owner, development] : theShares) {
		if (development > maxDev) {
			winner = owner;
			maxDev = development;
		}
	}
	if (!winner) {
		return std::nullopt;
	}

	// Now that we have a winning owner, let's find its largest province to use as a source.
	maxDev = -1; // We can have winning provinces with weight = 0;

	pair<unsigned long long, shared_ptr<Imperator::Province>> toReturn;
	for (const auto& province : theClaims.at(*winner)) {
		const auto provinceWeight = province->getBuildingsCount() + province->getPopCount();

		if (static_cast<int>(provinceWeight) > maxDev) {
			toReturn.first = province->getID();
			toReturn.second = province;
			maxDev = provinceWeight;
		}
	}
	if (!toReturn.first || !toReturn.second) {
		return std::nullopt;
	}
	return toReturn;
}


void CK3::World::addHistoryToVanillaTitles() {
	for (const auto& [name, title] : getTitles()) {
		auto historyOpt = titlesHistory.popTitleHistory(name);
		if (historyOpt)
			title->addHistory(landedTitles, *historyOpt);
	}
	// add vanilla development to counties
	// for counties that inherit development level from de jure lieges, assign it to them directly for better reliability
	for (const auto& title : getTitles() | std::views::values) {
		if (title->getRank() == TitleRank::county && !title->getDevelopmentLevel()) {
			title->setDevelopmentLevel(title->getOwnOrInheritedDevelopmentLevel());
		}
	}
}


void CK3::World::overWriteCountiesHistory() {
	Log(LogLevel::Info) << "Overwriting counties' history";
	for (const auto& title : getTitles() | std::views::values) {
		if (title->getRank()==TitleRank::county && title->capitalBaronyProvince > 0) { // title is a county and its capital province has a valid ID (0 is not a valid province in CK3)
			if (!provinces.contains(title->capitalBaronyProvince))
				Log(LogLevel::Warning) << "Capital barony province not found " << title->capitalBaronyProvince;
			else {
				const auto& impProvince = provinces.find(title->capitalBaronyProvince)->second->getImperatorProvince();
				if (impProvince) {
					if (const auto& impCountry = impProvince->getOwner().second; impCountry) {
						auto impMonarch = impCountry->getMonarch();
						if (impMonarch) {
							const auto& holderItr = characters.find("imperator" + std::to_string(*impMonarch));
							if (holderItr != characters.end()) {
								title->setHolder(holderItr->second);
							}
							title->setDeFactoLiege(nullptr);
							countyHoldersCache.emplace(title->getHolderID());
						}
					}
				}
				else { // county is probably outside of Imperator map
					if (!title->getHolderID().empty() && title->getHolderID() != "0")
						countyHoldersCache.emplace(title->getHolderID());
				}
			}
		}
	}
}


void CK3::World::removeInvalidLandlessTitles() {
	Log(LogLevel::Info) << "Removing invalid landless titles";
	set<string> removedGeneratedTitles;
	set<string> revokedVanillaTitles;

	for (const auto& [name, title] : getTitles()) {
		//important check: if duchy/kingdom/empire title holder holds no county (is landless), remove the title
		// this also removes landless titles initialized from Imperator
		if (title->getRank()!=TitleRank::county && title->getRank()!=TitleRank::barony && !countyHoldersCache.contains(title->getHolderID())) {
			if (!getTitles().find(name)->second->isLandless()) { // does not have landless attribute set to true
				if (title->isImportedOrUpdatedFromImperator() && name.find("IMPTOCK3") != string::npos) {
					removedGeneratedTitles.emplace(name);
					landedTitles.eraseTitle(name);
				}
				else {
					revokedVanillaTitles.emplace(name);
					title->setHolder(nullptr);
					title->setDeFactoLiege(nullptr);
				}
			}
		}
	}
	if (!removedGeneratedTitles.empty()) {
		string msg = "Found landless generated titles that can't be landless: ";
		for (const auto& name : removedGeneratedTitles) {
			msg += name;
			msg += ", ";
		}
		msg.erase(msg.length() - 2); // remove last ", "
		Log(LogLevel::Debug) << msg;
	}
	if (!revokedVanillaTitles.empty()) {
		string msg = "Found landless vanilla titles that can't be landless: ";
		for (const auto& name : revokedVanillaTitles) {
			msg += name;
			msg += ", ";
		}
		msg.erase(msg.length() - 2); // remove last ", "
		Log(LogLevel::Debug) << msg;
	}
}

void CK3::World::purgeLandlessVanillaCharacters() {
	set<string> farewellIDs;
	std::ranges::transform(std::as_const(characters), std::inserter(farewellIDs, farewellIDs.begin()),
	                       [](decltype(characters)::value_type const& pair) { return pair.first; });
	for (const auto& id : farewellIDs) {
		if (id.starts_with("imperator")) {
			farewellIDs.erase(id);
		}
	}

	for (const auto& titlePtr : getTitles() | std::views::values) {
		farewellIDs.erase(titlePtr->getHolderID());
	}
	for (const auto& characterId : farewellIDs) {
		characters[characterId]->breakAllLinks();
		characters.erase(characterId);
	}
	Log(LogLevel::Info) << "Purged " << farewellIDs.size() << " landless vanilla characters";
}


void CK3::World::linkSpouses() {
	auto counterSpouse = 0;
	for (const auto& ck3Character : characters | std::views::values) {
		map<unsigned long long, shared_ptr<Character>> newSpouses;
		// make links between Imperator characters
		for (const auto& impSpouseCharacter : ck3Character->imperatorCharacter->getSpouses() | std::views::values) {
			if (impSpouseCharacter != nullptr) {
				const auto& ck3SpouseCharacter = impSpouseCharacter->getCK3Character();
				ck3Character->addSpouse(ck3SpouseCharacter);
				ck3SpouseCharacter->addSpouse(ck3Character);
				++counterSpouse;
			}
		}
	}
	Log(LogLevel::Info) << "<> " << counterSpouse << " spouses linked in CK3.";
}


void CK3::World::linkMothersAndFathers() {
	auto counterMother = 0;
	auto counterFather = 0;
	for (const auto& ck3Character : characters | std::views::values) {
		// make links between Imperator characters
		const auto& impMotherCharacter = ck3Character->imperatorCharacter->getMother().second;
		if (impMotherCharacter != nullptr) {
			const auto& ck3MotherCharacter = impMotherCharacter->getCK3Character();
			ck3Character->setMother(ck3MotherCharacter);
			ck3MotherCharacter->addChild(ck3Character);
			++counterMother;
		}

		// make links between Imperator characters
		const auto& impFatherCharacter = ck3Character->imperatorCharacter->getFather().second;
		if (impFatherCharacter != nullptr) {
			const auto& ck3FatherCharacter = impFatherCharacter->getCK3Character();
			ck3Character->setFather(ck3FatherCharacter);
			ck3FatherCharacter->addChild(ck3Character);
			++counterFather;
		}
	}
	Log(LogLevel::Info) << "<> " << counterMother << " mothers and " << counterFather << " fathers linked in CK3.";
}


void CK3::World::importImperatorFamilies(const Imperator::World& impWorld) {
	Log(LogLevel::Info) << "-> Importing Imperator Families";

	// dynasties only holds dynasties converted from Imperator families, as vanilla ones aren't modified
	for (const auto& family : impWorld.getFamilies() | std::views::values) {
		if (family->isMinor())
			continue;

		auto newDynasty = make_shared<Dynasty>(*family, localizationMapper);
		dynasties.emplace(newDynasty->getID(), newDynasty);
	}
	Log(LogLevel::Info) << ">> " << dynasties.size() << " total families imported.";
}
