#ifndef IMPERATOR_POP_H
#define IMPERATOR_POP_H
#include "Parser.h"

namespace ImperatorWorld
{
	class Pop: commonItems::parser
	{
	  public:
		Pop(std::istream& theStream, int theFamilyID);

		[[nodiscard]] const auto& getType() const { return type; }
		[[nodiscard]] const auto& getCulture() const { return culture; }
		[[nodiscard]] const auto& getReligion() const { return religion; }

		[[nodiscard]] auto getID() const { return popID; }

	  private:
		void registerKeys();

		int popID = 0;
		std::string type;
		std::string culture;
		std::string religion;
	};
} // namespace ImperatorWorld

#endif // IMPERATOR_POP_H