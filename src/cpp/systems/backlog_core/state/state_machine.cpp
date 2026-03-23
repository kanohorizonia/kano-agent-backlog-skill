#include "state_machine.hpp"

namespace kano::backlog::core {

const std::unordered_map<ItemState, std::unordered_set<ItemState>> StateMachine::transitions_ = {
    {ItemState::New, {ItemState::Proposed, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::Proposed, {ItemState::Planned, ItemState::Ready, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::Planned, {ItemState::Ready, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::Ready, {ItemState::InProgress, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::InProgress, {ItemState::Review, ItemState::Done, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::Review, {ItemState::InProgress, ItemState::Done, ItemState::Blocked, ItemState::Dropped}},
    {ItemState::Blocked, {ItemState::Proposed, ItemState::Planned, ItemState::Ready, ItemState::InProgress, ItemState::Dropped}},
    {ItemState::Done, {}},
    {ItemState::Dropped, {}}
};

bool StateMachine::can_transition(ItemState from, ItemState to) {
    auto it = transitions_.find(from);
    if (it == transitions_.end()) return false;
    return it->second.count(to) > 0;
}

std::vector<ItemState> StateMachine::allowed_transitions(ItemState from) {
    auto it = transitions_.find(from);
    if (it == transitions_.end()) return {};
    return std::vector<ItemState>(it->second.begin(), it->second.end());
}

} // namespace kano::backlog::core
