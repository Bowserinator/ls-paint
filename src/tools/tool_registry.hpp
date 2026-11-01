#pragma once

#include <memory>
#include <unordered_map>

#include "tools/tool.hpp"

namespace lsp {

// Owns one instance of every canvas tool. Tools reach their state through the
// ToolContext they are handed, so the registry itself has no dependencies.
class ToolRegistry {
public:
    ToolRegistry();

    [[nodiscard]] ITool* find(ToolId id) const;

private:
    std::unordered_map<ToolId, std::unique_ptr<ITool>> m_tools;
};

}  // namespace lsp
