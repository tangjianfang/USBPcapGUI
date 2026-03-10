#pragma once

#include "bhplus_types.h"
#include "parser_interface.h"
#include <string>
#include <string_view>
#include <vector>

namespace bhplus {

struct FilterCondition {
    std::string key;
    std::string value;
    bool        negate = false;
    char        op = '=';
};

class FilterEngine {
public:
    static std::vector<FilterCondition> Parse(std::string_view filterText);

    static bool Matches(const BHPLUS_CAPTURE_EVENT& event,
                        const DecodedEvent& decoded,
                        std::string_view hexData,
                        const std::vector<FilterCondition>& conditions);

    static std::string Describe(const std::vector<FilterCondition>& conditions);
};

} // namespace bhplus
