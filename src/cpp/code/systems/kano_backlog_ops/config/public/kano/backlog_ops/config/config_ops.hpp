#pragma once

#include "kano/backlog_core/config/config.hpp"
#include <string>
#include <map>

namespace kano::backlog_ops {

class ConfigOps {
public:
    /**
     * Dump the effective configuration for a given context as a JSON string.
     */
    static std::string dump_effective_config_json(const kano::backlog_core::BacklogContext& ctx);

    /**
     * Get a human-readable summary of the configuration.
     */
    static std::string get_config_summary(const kano::backlog_core::BacklogContext& ctx);
};

} // namespace kano::backlog_ops
