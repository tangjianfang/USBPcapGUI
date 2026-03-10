#pragma once

#include "filter_engine.h"
#include <optional>
#include <string>
#include <vector>

namespace bhplus {

enum class TriggerAction {
    None,
    StartCapture,
    StopCapture,
    Snapshot
};

struct TriggerRule {
    std::string                  name;
    std::vector<FilterCondition> conditions;
    TriggerAction                action = TriggerAction::None;
    bool                         enabled = true;
    uint32_t                     maxMatches = 0;
    uint32_t                     matchedCount = 0;
};

class TriggerEngine {
public:
    void SetRules(std::vector<TriggerRule> rules);
    void Reset();

    std::optional<TriggerAction> Evaluate(const BHPLUS_CAPTURE_EVENT& event,
                                          const DecodedEvent& decoded,
                                          std::string_view hexData);

    const std::vector<TriggerRule>& Rules() const { return m_rules; }

private:
    std::vector<TriggerRule> m_rules;
};

} // namespace bhplus
