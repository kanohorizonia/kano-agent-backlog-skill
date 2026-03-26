#include "kano/backlog_core/state/state_machine.hpp"
#include "kano/backlog_core/validation/validator.hpp"
#include "kano/backlog_core/models/errors.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <ctime>

namespace kano::backlog_core {

const std::map<StateMachine::TransitionKey, ItemState>& StateMachine::get_transitions() {
    static const std::map<TransitionKey, ItemState> transitions = {
        {{ItemState::New, StateAction::Propose}, ItemState::Proposed},
        
        {{ItemState::Proposed, StateAction::Ready}, ItemState::Ready},
        {{ItemState::New, StateAction::Ready}, ItemState::Ready},
        
        {{ItemState::Ready, StateAction::Start}, ItemState::InProgress},
        {{ItemState::Proposed, StateAction::Start}, ItemState::InProgress},
        {{ItemState::New, StateAction::Start}, ItemState::InProgress},
        {{ItemState::Blocked, StateAction::Start}, ItemState::InProgress},
        
        {{ItemState::InProgress, StateAction::Review}, ItemState::Review},
        
        {{ItemState::InProgress, StateAction::Done}, ItemState::Done},
        {{ItemState::Review, StateAction::Done}, ItemState::Done},
        {{ItemState::Ready, StateAction::Done}, ItemState::Done},
        
        {{ItemState::New, StateAction::Block}, ItemState::Blocked},
        {{ItemState::Proposed, StateAction::Block}, ItemState::Blocked},
        {{ItemState::Ready, StateAction::Block}, ItemState::Blocked},
        {{ItemState::InProgress, StateAction::Block}, ItemState::Blocked},
        {{ItemState::Review, StateAction::Block}, ItemState::Blocked},
        
        {{ItemState::New, StateAction::Drop}, ItemState::Dropped},
        {{ItemState::Proposed, StateAction::Drop}, ItemState::Dropped},
        {{ItemState::Ready, StateAction::Drop}, ItemState::Dropped},
        {{ItemState::InProgress, StateAction::Drop}, ItemState::Dropped},
        {{ItemState::Review, StateAction::Drop}, ItemState::Dropped},
        {{ItemState::Blocked, StateAction::Drop}, ItemState::Dropped}
    };
    return transitions;
}

bool StateMachine::can_transition(ItemState state, StateAction action) {
    TransitionKey key{state, action};
    const auto& table = get_transitions();
    return table.find(key) != table.end();
}

void StateMachine::transition(
    BacklogItem& item, 
    StateAction action, 
    const std::optional<std::string>& agent, 
    const std::optional<std::string>& message,
    const std::optional<std::string>& model
) {
    // 1. Check if transition exists
    TransitionKey key{item.state, action};
    const auto& table = get_transitions();
    auto it = table.find(key);
    
    if (it == table.end()) {
        throw ValidationError({ "Invalid transition: " + to_string(item.state) + " --" + to_string(action) + "--> (no target state)" });
    }

    // 2. Check Ready gate
    if (action == StateAction::Ready) {
        auto [is_valid, errors] = Validator::is_ready(item);
        if (!is_valid) {
            std::string err_msg = "Ready gate failed: ";
            for (size_t i = 0; i < errors.size(); ++i) {
                err_msg += errors[i];
                if (i < errors.size() - 1) err_msg += ", ";
            }
            throw ValidationError({ err_msg });
        }
    }

    // 3. Execute transition
    ItemState old_state = item.state;
    ItemState new_state = it->second;
    item.state = new_state;

    // 4. Update timestamp (ISO 8601 YYYY-MM-DD)
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    std::stringstream date_ss;
    date_ss << std::put_time(&buf, "%Y-%m-%d");
    item.updated = date_ss.str();

    // 5. Append worklog entry
    std::stringstream time_ss;
    time_ss << std::put_time(&buf, "%Y-%m-%d %H:%M");
    
    std::string state_msg = "State: " + to_string(old_state) + " -> " + to_string(new_state);
    std::string worklog_text = message ? (state_msg + ": " + *message) : state_msg;
    
    std::stringstream worklog_line;
    worklog_line << time_ss.str() << " ";
    if (agent) {
        std::string model_val = (model && !model->empty()) ? *model : "unknown";
        worklog_line << "[agent=" << *agent << "] [model=" << model_val << "] ";
    }
    worklog_line << worklog_text;
    
    item.worklog.push_back(worklog_line.str());
}

void StateMachine::record_worklog(
    BacklogItem& item,
    const std::string& agent,
    const std::string& message,
    const std::optional<std::string>& model
) {
    auto now_t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm buf;
#ifdef _WIN32
    localtime_s(&buf, &now_t);
#else
    localtime_r(&now_t, &buf);
#endif
    
    std::stringstream time_ss;
    time_ss << std::put_time(&buf, "%Y-%m-%d %H:%M");
    
    std::stringstream worklog_line;
    worklog_line << time_ss.str() << " ";
    std::string model_val = (model && !model->empty()) ? *model : "unknown";
    worklog_line << "[agent=" << agent << "] [model=" << model_val << "] " << message;
    
    item.worklog.push_back(worklog_line.str());
}

} // namespace kano::backlog_core
