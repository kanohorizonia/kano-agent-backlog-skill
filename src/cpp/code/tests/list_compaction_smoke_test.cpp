#include <kano_unattended.hpp>

#include "kano/backlog_ops/view/view_ops.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using kano::backlog_core::ItemState;
using kano::backlog_core::ItemType;
using kano::backlog_core::parse_item_state;
using kano::backlog_core::parse_item_type;
using kano::backlog_ops::CompactListOptions;
using kano::backlog_ops::CompactListResult;
using kano::backlog_ops::ListItemProjection;
using kano::backlog_ops::ViewOps;

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<std::string> split_preserving_empty(
    const std::string& value,
    char delimiter
) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        const auto position = value.find(delimiter, start);
        if (position == std::string::npos) {
            parts.push_back(value.substr(start));
            break;
        }
        parts.push_back(value.substr(start, position - start));
        start = position + 1;
    }
    return parts;
}

std::vector<std::string> split_list(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    return split_preserving_empty(value, ',');
}

std::optional<std::string> optional_text(const std::string& value) {
    if (value.empty() || value == "~") {
        return std::nullopt;
    }
    return value;
}

std::vector<ListItemProjection> load_fixture(
    const std::filesystem::path& fixture_path
) {
    std::ifstream input(fixture_path);
    if (!input.is_open()) {
        throw std::runtime_error("failed to open fixture: " + fixture_path.string());
    }

    std::vector<ListItemProjection> items;
    std::string line;
    while (std::getline(input, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const auto columns = split_preserving_empty(line, '|');
        expect(columns.size() == 12, "fixture row must contain 12 columns");
        const auto type = parse_item_type(columns[2]);
        const auto state = parse_item_state(columns[4]);
        expect(type.has_value(), "fixture item type must be valid");
        expect(state.has_value(), "fixture item state must be valid");

        ListItemProjection item;
        item.id = columns[0];
        item.uid = columns[1];
        item.type = *type;
        item.title = columns[3];
        item.state = *state;
        item.priority = optional_text(columns[5]);
        item.updated = columns[6];
        item.parent = optional_text(columns[7]);
        item.links.relates = split_list(columns[8]);
        item.links.blocks = split_list(columns[9]);
        item.links.blocked_by = split_list(columns[10]);
        item.topics = split_list(columns[11]);
        item.canonical_position = items.size();
        items.push_back(std::move(item));
    }
    return items;
}

bool contains_item(
    const std::vector<ListItemProjection>& items,
    const std::string& id
) {
    return std::any_of(items.begin(), items.end(), [&](const auto& item) {
        return item.id == id;
    });
}

const ListItemProjection& find_item(
    const std::vector<ListItemProjection>& items,
    const std::string& id
) {
    const auto found = std::find_if(items.begin(), items.end(), [&](const auto& item) {
        return item.id == id;
    });
    if (found == items.end()) {
        throw std::runtime_error("item not found in result: " + id);
    }
    return *found;
}

const kano::backlog_ops::CompactListGroup& find_group(
    const CompactListResult& result,
    const std::string& id
) {
    const auto found = std::find_if(result.groups.begin(), result.groups.end(), [&](const auto& group) {
        return group.id == id;
    });
    if (found == result.groups.end()) {
        throw std::runtime_error("group not found in result: " + id);
    }
    return *found;
}

} // namespace

int main() {
    kano::infra::ConfigureUnattendedExecution();
    try {
        const auto repo_root = std::filesystem::path(KANO_REPO_ROOT);
        const auto fixture_path =
            repo_root / "src/cpp/code/tests/fixtures/workitem_list/large_item_list.fixture";
        const auto canonical_items = load_fixture(fixture_path);
        expect(canonical_items.size() == 15, "large item list fixture should contain 15 items");

        const auto original_ids = [&]() {
            std::vector<std::string> ids;
            for (const auto& item : canonical_items) {
                ids.push_back(item.id);
            }
            return ids;
        }();
        const auto original_states = [&]() {
            std::vector<ItemState> states;
            for (const auto& item : canonical_items) {
                states.push_back(item.state);
            }
            return states;
        }();

        const auto compact = ViewOps::compact_items(canonical_items);
        expect(compact.canonical_total == 15, "compact result should retain canonical total");
        expect(compact.selection_total == 15, "default compaction should select every canonical item");
        expect(compact.shown_count == 9 && compact.omitted_count == 6,
            "default compaction should omit only safe old Done items");
        expect(compact.latest_updated == "2026-07-28", "recency should anchor to the newest canonical date");
        expect(compact.cutoff_source == "latest_updated_window" &&
                   compact.recent_days == std::optional<int>(30),
            "default compaction should declare its latest-date recency window");
        expect(contains_item(compact.items, "LST-TSK-0001"), "Ready item must remain visible");
        expect(contains_item(compact.items, "LST-TSK-0002"), "Review item must remain visible");
        expect(contains_item(compact.items, "LST-BUG-0001"), "Blocked item must remain visible");
        expect(contains_item(compact.items, "LST-TSK-0008"), "dependency-bearing old Done item must remain visible");
        expect(contains_item(compact.items, "LST-TSK-0010"), "unknown-date Done item must fail open");
        expect(contains_item(compact.items, "LST-TSK-0013"),
            "updated values with trailing garbage must fail open");
        expect(!contains_item(compact.items, "LST-TSK-0006"), "old Done item should be compacted");
        expect(!contains_item(compact.items, "LST-TSK-0009"), "old topic Done item should be compacted");
        expect(find_item(compact.items, "LST-BUG-0001").links.blocked_by ==
                   std::vector<std::string>{"LST-TSK-0999"},
            "visible blocker edges must remain exact");
        expect(find_item(compact.items, "LST-TSK-0008").links.blocks ==
                   std::vector<std::string>{"LST-TSK-0004"},
            "visible dependency edges must remain exact");

        const std::string old_done_group =
            "state:done/type:task/priority:1:P2/topics:0/updated:before-2026-06-28";
        const auto& group = find_group(compact, old_done_group);
        expect(group.total_count == 3 && group.shown_count == 1 && group.omitted_count == 2,
            "old Done group should report reversible shown and omitted counts");
        const auto& multi_topic_group = find_group(
            compact,
            "state:review/type:task/priority:1:P1/topics:2:alpha+operator-routing/"
            "updated:on-or-after-2026-06-28"
        );
        expect(multi_topic_group.topics ==
                   std::vector<std::string>({"alpha", "operator-routing"}),
            "group identity should preserve the complete sorted topic membership set");
        const auto& literal_none_group = find_group(
            compact,
            "state:done/type:task/priority:1:P2/topics:1:none/updated:before-2026-06-28"
        );
        const auto& literal_title_none_group = find_group(
            compact,
            "state:done/type:task/priority:1:P2/topics:1:None/updated:before-2026-06-28"
        );
        expect(literal_none_group.id != old_done_group &&
                   literal_title_none_group.id != literal_none_group.id,
            "empty, literal none, and title-case None topic sets must not collide");

        std::vector<ListItemProjection> priority_variants;
        for (const auto& [id_suffix, priority] : std::vector<
                 std::pair<std::string, std::optional<std::string>>
             >{
                 {"0001", std::nullopt},
                 {"0002", std::string{}},
                 {"0003", std::string{"none"}},
                 {"0004", std::string{"None"}},
                 {"0005", std::string{"P1"}},
                 {"0006", std::string{"p1"}}
             }) {
            auto variant = canonical_items.front();
            variant.id = "PRI-TSK-" + id_suffix;
            variant.uid = "019f3000-" + id_suffix + "-7000-8000-00000000" + id_suffix;
            variant.state = ItemState::Done;
            variant.priority = priority;
            variant.updated.clear();
            variant.parent.reset();
            variant.links = {};
            variant.topics.clear();
            priority_variants.push_back(std::move(variant));
        }
        const auto priority_compact = ViewOps::compact_items(priority_variants);
        expect(priority_compact.groups.size() == 6,
            "absent, empty, none, None, P1, and p1 priorities must form distinct groups");
        const auto priority_group_suffix = "/topics:0/updated:unknown";
        const auto& absent_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:0" + std::string(priority_group_suffix)
        );
        const auto& empty_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:1:" + std::string(priority_group_suffix)
        );
        const auto& none_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:1:none" + std::string(priority_group_suffix)
        );
        const auto& title_none_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:1:None" + std::string(priority_group_suffix)
        );
        const auto& upper_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:1:P1" + std::string(priority_group_suffix)
        );
        const auto& lower_priority_group = find_group(
            priority_compact,
            "state:done/type:task/priority:1:p1" + std::string(priority_group_suffix)
        );
        expect(!absent_priority_group.priority.has_value() &&
                   empty_priority_group.priority == std::optional<std::string>{""} &&
                   none_priority_group.priority == std::optional<std::string>{"none"} &&
                   title_none_priority_group.priority == std::optional<std::string>{"None"} &&
                   upper_priority_group.priority == std::optional<std::string>{"P1"} &&
                   lower_priority_group.priority == std::optional<std::string>{"p1"},
            "priority groups must retain exact optional values for JSON projection");

        CompactListOptions exact_options;
        exact_options.exact_item = "LST-TSK-0006";
        const auto exact = ViewOps::compact_items(canonical_items, exact_options);
        expect(exact.expanded_retrieval && exact.selection_total == 1 &&
                   exact.shown_count == 1 && exact.omitted_count == 0,
            "exact item retrieval should fully expand an omitted item");
        expect(exact.items.front().id == "LST-TSK-0006",
            "exact item retrieval should return only the requested ID");

        CompactListOptions state_options;
        state_options.state = ItemState::Done;
        const auto done = ViewOps::compact_items(canonical_items, state_options);
        expect(done.expanded_retrieval && done.selection_total == 10 &&
                   done.shown_count == 10 && done.omitted_count == 0,
            "state retrieval should fully expand every matching Done item");

        CompactListOptions topic_options;
        topic_options.topic = "alpha";
        const auto topic = ViewOps::compact_items(canonical_items, topic_options);
        expect(topic.expanded_retrieval && topic.selection_total == 2 &&
                   topic.shown_count == 2 && topic.omitted_count == 0,
            "topic retrieval should fully expand every matching item");
        expect(contains_item(topic.items, "LST-TSK-0002") &&
                   contains_item(topic.items, "LST-TSK-0009"),
            "topic retrieval should include visible and formerly omitted items");

        CompactListOptions group_options;
        group_options.group = old_done_group;
        const auto expanded_group = ViewOps::compact_items(canonical_items, group_options);
        expect(expanded_group.expanded_retrieval && expanded_group.selection_total == 3 &&
                   expanded_group.shown_count == 3 && expanded_group.omitted_count == 0,
            "group retrieval should fully expand the selected compact group");

        auto later_canonical_items = canonical_items;
        auto later_item = later_canonical_items.front();
        later_item.id = "LST-TSK-9999";
        later_item.uid = "019f1000-9999-7000-8000-000000009999";
        later_item.title = "Later canonical item";
        later_item.state = ItemState::Done;
        later_item.priority = "P2";
        later_item.updated = "2026-08-28";
        later_item.topics.clear();
        later_item.links = {};
        later_canonical_items.insert(later_canonical_items.begin(), later_item);
        const auto replayed_group = ViewOps::compact_items(later_canonical_items, group_options);
        expect(replayed_group.selection_total == 3 &&
                   replayed_group.cutoff_date == "2026-06-28",
            "group retrieval should replay its embedded cutoff after newer items arrive");
        expect(replayed_group.cutoff_source == "group_selector" &&
                   !replayed_group.recent_days.has_value(),
            "group replay metadata should not claim a current-anchor recent window");

        CompactListOptions type_options;
        type_options.type = ItemType::Bug;
        const auto bug_only = ViewOps::compact_items(canonical_items, type_options);
        expect(!bug_only.expanded_retrieval && bug_only.selection_total == 2 &&
                   bug_only.shown_count == 1 && bug_only.omitted_count == 1,
            "type filtering alone should retain normal compaction behavior");

        const auto rendered = ViewOps::render_compact_table(compact);
        expect(rendered.find("Compact work item list (opt-in)") != std::string::npos,
            "plain compact output should identify the opt-in projection");
        expect(rendered.find(old_done_group) != std::string::npos &&
                   rendered.find("operation=workitem.list") != std::string::npos,
            "plain compact output should expose a structured group retrieval selector");

        std::vector<std::string> final_ids;
        std::vector<ItemState> final_states;
        for (const auto& item : canonical_items) {
            final_ids.push_back(item.id);
            final_states.push_back(item.state);
        }
        expect(final_ids == original_ids, "compaction must not reorder or mutate canonical IDs");
        expect(final_states == original_states, "compaction must not mutate canonical states");

        std::cout << "list compaction smoke test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "list compaction smoke test failed: " << error.what() << "\n";
        return 1;
    }
}
