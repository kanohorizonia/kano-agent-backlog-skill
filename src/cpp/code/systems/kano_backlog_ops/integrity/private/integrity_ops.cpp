#include "kano/backlog_ops/integrity/integrity_ops.hpp"

#include "kano/backlog_core/frontmatter/canonical_store.hpp"
#include "kano/backlog_core/models/models.hpp"
#include "kano/backlog_core/validation/validator.hpp"

#include <json/json.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <tuple>

namespace kano::backlog_ops {

namespace {

using kano::backlog_core::BacklogItem;
using kano::backlog_core::CanonicalStore;
using kano::backlog_core::ItemState;
using kano::backlog_core::ItemType;
using kano::backlog_core::Validator;
using kano::backlog_core::to_string;

struct ScannedItem {
    BacklogItem item;
    std::filesystem::path path;
};

std::filesystem::path normalized_absolute_path(const std::filesystem::path& path) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        absolute = path;
        ec.clear();
    }
    auto normalized = std::filesystem::weakly_canonical(absolute, ec);
    if (ec) {
        normalized = absolute.lexically_normal();
    }
    return normalized;
}

std::string current_utc_date() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#ifdef _WIN32
    gmtime_s(&tm_buf, &now_time);
#else
    gmtime_r(&now_time, &tm_buf);
#endif
    std::ostringstream out;
    out << std::put_time(&tm_buf, "%Y-%m-%d");
    return out.str();
}

std::string lower_copy(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string trim_copy(const std::string& value) {
    auto begin = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch);
    });
    auto end = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch);
    }).base();
    if (begin >= end) {
        return "";
    }
    return std::string(begin, end);
}

std::string normalized_title_key(const std::string& title) {
    std::string key = lower_copy(trim_copy(title));
    std::string compact;
    bool pending_space = false;
    for (unsigned char ch : key) {
        if (std::isspace(ch)) {
            pending_space = !compact.empty();
            continue;
        }
        if (pending_space) {
            compact.push_back(' ');
            pending_space = false;
        }
        compact.push_back(static_cast<char>(ch));
    }
    return compact;
}

std::string item_type_directory(ItemType type) {
    switch (type) {
        case ItemType::Initiative: return "initiative";
        case ItemType::Epic: return "epic";
        case ItemType::Feature: return "feature";
        case ItemType::UserStory: return "userstory";
        case ItemType::Task: return "task";
        case ItemType::SubTask: return "subtask";
        case ItemType::Bug: return "bug";
        case ItemType::Issue: return "issue";
    }
    return "item";
}

std::optional<std::string> id_from_filename(const std::filesystem::path& path) {
    const auto stem = path.stem().string();
    const auto underscore = stem.find('_');
    if (underscore == std::string::npos) {
        return std::nullopt;
    }
    return stem.substr(0, underscore);
}

std::string product_name_for_root(const std::filesystem::path& product_root) {
    return product_root.filename().string();
}

std::vector<std::filesystem::path> list_product_roots(
    const std::filesystem::path& backlog_root,
    const std::vector<std::string>& products
) {
    std::vector<std::filesystem::path> roots;
    if (!products.empty()) {
        std::set<std::string> seen;
        for (const auto& product : products) {
            if (product.empty() || !seen.insert(product).second) {
                continue;
            }
            auto product_root = backlog_root / "products" / product;
            if (!std::filesystem::exists(product_root) && backlog_root.filename().string() == product) {
                product_root = backlog_root;
            }
            if (!std::filesystem::exists(product_root / "items")) {
                throw std::runtime_error("Product root not found or missing items directory: " + product_root.string());
            }
            roots.push_back(normalized_absolute_path(product_root));
        }
        std::sort(roots.begin(), roots.end());
        return roots;
    }

    const auto products_root = backlog_root / "products";
    if (std::filesystem::exists(products_root)) {
        for (const auto& entry : std::filesystem::directory_iterator(products_root)) {
            if (entry.is_directory() && std::filesystem::exists(entry.path() / "items")) {
                roots.push_back(normalized_absolute_path(entry.path()));
            }
        }
    }
    if (std::filesystem::exists(backlog_root / "items")) {
        roots.push_back(normalized_absolute_path(backlog_root));
    }
    std::sort(roots.begin(), roots.end());
    return roots;
}

std::vector<ScannedItem> read_product_items(const std::filesystem::path& product_root) {
    CanonicalStore store(product_root);
    auto paths = store.list_items();
    std::sort(paths.begin(), paths.end());
    std::vector<ScannedItem> items;
    for (const auto& path : paths) {
        const auto filename = path.filename().string();
        if (filename == "README.md" || filename.ends_with(".index.md")) {
            continue;
        }
        items.push_back(ScannedItem{store.read(path), path});
    }
    return items;
}

void add_finding(
    IntegrityReport& report,
    std::string issue_type,
    std::string rule_id,
    std::string severity,
    const std::string& product,
    const BacklogItem& item,
    const std::filesystem::path& path,
    std::string message
) {
    report.findings.push_back(IntegrityFinding{
        std::move(issue_type),
        std::move(rule_id),
        std::move(severity),
        product,
        item.id,
        path,
        std::move(message)
    });
}

bool resolves_item_ref(
    const std::string& ref,
    const std::map<std::string, const ScannedItem*>& by_id,
    const std::map<std::string, const ScannedItem*>& by_uid
) {
    return by_id.find(ref) != by_id.end() || by_uid.find(ref) != by_uid.end();
}

int days_from_civil(int year, unsigned month, unsigned day) {
    year -= month <= 2;
    const int era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153 * (month + (month > 2 ? -3 : 9)) + 2) / 5 + day - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int>(doe) - 719468;
}

std::optional<int> parse_iso_date_days(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return std::nullopt;
    }
    const auto all_digits = [](std::string_view value) {
        return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return std::isdigit(ch);
        });
    };
    if (!all_digits(std::string_view(date).substr(0, 4)) ||
        !all_digits(std::string_view(date).substr(5, 2)) ||
        !all_digits(std::string_view(date).substr(8, 2))) {
        return std::nullopt;
    }
    const int year = std::stoi(date.substr(0, 4));
    const unsigned month = static_cast<unsigned>(std::stoi(date.substr(5, 2)));
    const unsigned day = static_cast<unsigned>(std::stoi(date.substr(8, 2)));
    if (month < 1 || month > 12 || day < 1 || day > 31) {
        return std::nullopt;
    }
    return days_from_civil(year, month, day);
}

bool is_ready_or_active_state(ItemState state) {
    return state == ItemState::Ready || state == ItemState::InProgress || state == ItemState::Review;
}

void inspect_product(
    IntegrityReport& report,
    const std::filesystem::path& product_root,
    const std::string& product,
    int as_of_days
) {
    const auto items = read_product_items(product_root);
    report.items_scanned += static_cast<int>(items.size());
    report.products_scanned.push_back(product);

    std::map<std::string, std::vector<const ScannedItem*>> id_groups;
    std::map<std::string, std::vector<const ScannedItem*>> uid_groups;
    std::map<std::string, std::vector<const ScannedItem*>> title_groups;
    std::map<std::string, const ScannedItem*> by_id;
    std::map<std::string, const ScannedItem*> by_uid;

    for (const auto& scanned : items) {
        if (!scanned.item.id.empty()) {
            id_groups[scanned.item.id].push_back(&scanned);
            by_id.emplace(scanned.item.id, &scanned);
        }
        if (!scanned.item.uid.empty()) {
            uid_groups[scanned.item.uid].push_back(&scanned);
            by_uid.emplace(scanned.item.uid, &scanned);
        }
        const auto title_key = normalized_title_key(scanned.item.title);
        if (!title_key.empty()) {
            title_groups[title_key].push_back(&scanned);
        }
    }

    auto emit_duplicate_group = [&](const auto& groups, const std::string& rule_id, const std::string& field_name, const std::string& severity) {
        for (const auto& [value, group] : groups) {
            if (group.size() < 2) {
                continue;
            }
            std::vector<std::string> paths;
            for (const auto* scanned : group) {
                paths.push_back(scanned->path.filename().string());
            }
            std::sort(paths.begin(), paths.end());
            const std::string joined_paths = [&]() {
                std::ostringstream out;
                for (std::size_t i = 0; i < paths.size(); ++i) {
                    if (i > 0) {
                        out << ", ";
                    }
                    out << paths[i];
                }
                return out.str();
            }();
            for (const auto* scanned : group) {
                add_finding(
                    report,
                    "duplicate",
                    rule_id,
                    severity,
                    product,
                    scanned->item,
                    scanned->path,
                    "Duplicate " + field_name + " '" + value + "' also appears in: " + joined_paths
                );
            }
        }
    };

    emit_duplicate_group(id_groups, "duplicate.id", "id", "error");
    emit_duplicate_group(uid_groups, "duplicate.uid", "uid", "error");
    emit_duplicate_group(title_groups, "duplicate.title", "title", "warning");

    for (const auto& scanned : items) {
        const auto filename_id = id_from_filename(scanned.path);
        if (!filename_id || *filename_id != scanned.item.id) {
            add_finding(
                report,
                "drift",
                "drift.path_id",
                "error",
                product,
                scanned.item,
                scanned.path,
                "Path filename id does not match frontmatter id '" + scanned.item.id + "'"
            );
        }

        const auto expected_type_dir = item_type_directory(scanned.item.type);
        const auto actual_type_dir = scanned.path.parent_path().parent_path().filename().string();
        if (actual_type_dir != expected_type_dir) {
            add_finding(
                report,
                "drift",
                "drift.path_type",
                "error",
                product,
                scanned.item,
                scanned.path,
                "Path type directory '" + actual_type_dir + "' does not match frontmatter type '" + to_string(scanned.item.type) + "'"
            );
        }

        if (scanned.item.parent && !resolves_item_ref(*scanned.item.parent, by_id, by_uid)) {
            add_finding(
                report,
                "stale",
                "stale.parent_ref",
                "warning",
                product,
                scanned.item,
                scanned.path,
                "Parent reference does not resolve in product: " + *scanned.item.parent
            );
        }

        if (scanned.item.duplicate_of && !resolves_item_ref(*scanned.item.duplicate_of, by_id, by_uid)) {
            add_finding(
                report,
                "stale",
                "stale.duplicate_of_ref",
                "warning",
                product,
                scanned.item,
                scanned.path,
                "duplicate_of reference does not resolve in product: " + *scanned.item.duplicate_of
            );
        }

        const auto updated_days = parse_iso_date_days(scanned.item.updated);
        if (updated_days && report.stale_days >= 0 && as_of_days - *updated_days > report.stale_days) {
            add_finding(
                report,
                "stale",
                "stale.updated_age",
                "info",
                product,
                scanned.item,
                scanned.path,
                "Item updated date '" + scanned.item.updated + "' is older than stale-days threshold " + std::to_string(report.stale_days)
            );
        }

        if (is_ready_or_active_state(scanned.item.state)) {
            const auto [is_ready, missing_fields] = Validator::is_ready(scanned.item);
            if (!is_ready) {
                std::ostringstream missing;
                for (std::size_t i = 0; i < missing_fields.size(); ++i) {
                    if (i > 0) {
                        missing << ", ";
                    }
                    missing << missing_fields[i];
                }
                add_finding(
                    report,
                    "drift",
                    "drift.ready_gate",
                    "error",
                    product,
                    scanned.item,
                    scanned.path,
                    "Item state '" + to_string(scanned.item.state) + "' does not satisfy Ready gate; missing: " + missing.str()
                );
            }
        }

        const bool has_duplicate_ref = scanned.item.duplicate_of && !trim_copy(*scanned.item.duplicate_of).empty();
        if (scanned.item.state == ItemState::Duplicate && !has_duplicate_ref) {
            add_finding(
                report,
                "drift",
                "drift.duplicate_state",
                "error",
                product,
                scanned.item,
                scanned.path,
                "Duplicate state requires duplicate_of reference"
            );
        } else if (scanned.item.state != ItemState::Duplicate && has_duplicate_ref) {
            add_finding(
                report,
                "drift",
                "drift.duplicate_state",
                "warning",
                product,
                scanned.item,
                scanned.path,
                "duplicate_of is set but state is '" + to_string(scanned.item.state) + "' instead of Duplicate"
            );
        }
    }
}

bool finding_less(const IntegrityFinding& lhs, const IntegrityFinding& rhs) {
    return std::tie(lhs.issue_type, lhs.rule_id, lhs.product, lhs.item_id, lhs.path, lhs.message) <
        std::tie(rhs.issue_type, rhs.rule_id, rhs.product, rhs.item_id, rhs.path, rhs.message);
}

Json::Value finding_to_json(const IntegrityFinding& finding) {
    Json::Value value(Json::objectValue);
    value["issue_type"] = finding.issue_type;
    value["rule_id"] = finding.rule_id;
    value["severity"] = finding.severity;
    value["product"] = finding.product;
    value["item_id"] = finding.item_id;
    value["path"] = finding.path.string();
    value["message"] = finding.message;
    return value;
}

std::string json_to_string(const Json::Value& value) {
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "  ";
    return Json::writeString(builder, value);
}

} // namespace

IntegrityReport IntegrityOps::inspect(const IntegrityOptions& options) {
    IntegrityReport report;
    report.backlog_root = normalized_absolute_path(options.backlog_root.empty() ? std::filesystem::path(".") : options.backlog_root);
    report.as_of = options.as_of.empty() ? current_utc_date() : options.as_of;
    report.stale_days = options.stale_days;

    const auto as_of_days = parse_iso_date_days(report.as_of);
    if (!as_of_days) {
        throw std::runtime_error("Invalid --as-of date, expected YYYY-MM-DD: " + report.as_of);
    }
    if (report.stale_days < 0) {
        throw std::runtime_error("--stale-days must be >= 0");
    }

    const auto product_roots = list_product_roots(report.backlog_root, options.products);
    for (const auto& product_root : product_roots) {
        inspect_product(report, product_root, product_name_for_root(product_root), *as_of_days);
    }
    std::sort(report.products_scanned.begin(), report.products_scanned.end());
    report.products_scanned.erase(std::unique(report.products_scanned.begin(), report.products_scanned.end()), report.products_scanned.end());
    std::sort(report.findings.begin(), report.findings.end(), finding_less);
    return report;
}

std::string IntegrityOps::render_markdown(const IntegrityReport& report) {
    std::ostringstream out;
    out << "# Backlog Integrity Report\n\n";
    out << "- backlog_root: " << report.backlog_root.string() << "\n";
    out << "- as_of: " << report.as_of << "\n";
    out << "- stale_days: " << report.stale_days << "\n";
    out << "- products_scanned: ";
    for (std::size_t i = 0; i < report.products_scanned.size(); ++i) {
        if (i > 0) {
            out << ", ";
        }
        out << report.products_scanned[i];
    }
    out << "\n";
    out << "- items_scanned: " << report.items_scanned << "\n";
    out << "- findings: " << report.findings.size() << "\n\n";

    if (report.findings.empty()) {
        out << "No integrity findings.\n";
        return out.str();
    }

    std::map<std::string, std::vector<const IntegrityFinding*>> groups;
    for (const auto& finding : report.findings) {
        groups[finding.issue_type].push_back(&finding);
    }
    for (const auto& [issue_type, findings] : groups) {
        out << "## " << issue_type << "\n\n";
        for (const auto* finding : findings) {
            out << "- [" << finding->severity << "] " << finding->rule_id
                << " product=" << finding->product
                << " item=" << finding->item_id
                << " path=" << finding->path.string()
                << " — " << finding->message << "\n";
        }
        out << "\n";
    }
    return out.str();
}

std::string IntegrityOps::render_json(const IntegrityReport& report) {
    Json::Value root(Json::objectValue);
    root["backlog_root"] = report.backlog_root.string();
    root["as_of"] = report.as_of;
    root["stale_days"] = report.stale_days;
    root["items_scanned"] = report.items_scanned;
    Json::Value products(Json::arrayValue);
    for (const auto& product : report.products_scanned) {
        products.append(product);
    }
    root["products_scanned"] = products;

    Json::Value findings(Json::arrayValue);
    Json::Value groups(Json::arrayValue);
    std::map<std::string, std::vector<const IntegrityFinding*>> grouped;
    for (const auto& finding : report.findings) {
        findings.append(finding_to_json(finding));
        grouped[finding.issue_type].push_back(&finding);
    }
    for (const auto& [issue_type, group_findings] : grouped) {
        Json::Value group(Json::objectValue);
        group["issue_type"] = issue_type;
        Json::Value group_values(Json::arrayValue);
        for (const auto* finding : group_findings) {
            group_values.append(finding_to_json(*finding));
        }
        group["findings"] = group_values;
        groups.append(group);
    }
    root["findings"] = findings;
    root["groups"] = groups;
    return json_to_string(root);
}

} // namespace kano::backlog_ops
