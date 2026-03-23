#pragma once
#include "../model/backlog_item.hpp"
#include <vector>
#include <string>

namespace kano::backlog::core {

struct ValidationResult {
    bool valid;
    std::vector<std::string> errors;
};

class Validator {
public:
    static ValidationResult validate_ready_gate(const BacklogItem& item);
    static ValidationResult validate_item(const BacklogItem& item);
    
private:
    static void require_field(
        const std::optional<std::string>& value,
        const std::string& display_name,
        std::vector<std::string>& errors);
};

} // namespace kano::backlog::core
