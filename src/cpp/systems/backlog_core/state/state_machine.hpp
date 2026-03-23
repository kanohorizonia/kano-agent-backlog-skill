#pragma once
#include "../model/backlog_item.hpp"
#include <unordered_map>
#include <unordered_set>

namespace kano::backlog::core {

class StateMachine {
public:
    static bool can_transition(ItemState from, ItemState to);
    static std::vector<ItemState> allowed_transitions(ItemState from);
    
private:
    static const std::unordered_map<ItemState, std::unordered_set<ItemState>> transitions_;
};

} // namespace kano::backlog::core
