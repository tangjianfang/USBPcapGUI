/*
 * USBPcapGUI - Trigger Engine
 */

#include "trigger_engine.h"

namespace bhplus {

void TriggerEngine::SetRules(std::vector<TriggerRule> rules) {
	m_rules = std::move(rules);
}

void TriggerEngine::Reset() {
	for (auto& rule : m_rules) {
		rule.matchedCount = 0;
	}
}

std::optional<TriggerAction> TriggerEngine::Evaluate(const BHPLUS_CAPTURE_EVENT& event,
													 const DecodedEvent& decoded,
													 std::string_view hexData) {
	for (auto& rule : m_rules) {
		if (!rule.enabled || rule.action == TriggerAction::None) {
			continue;
		}
		if (rule.maxMatches > 0 && rule.matchedCount >= rule.maxMatches) {
			continue;
		}
		if (!FilterEngine::Matches(event, decoded, hexData, rule.conditions)) {
			continue;
		}

		++rule.matchedCount;
		return rule.action;
	}

	return std::nullopt;
}

} // namespace bhplus
