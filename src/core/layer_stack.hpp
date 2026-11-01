#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "core/layer.hpp"

namespace lsp {

class LayerStack {
public:
    void add(Layer layer) {
        m_layers.push_back(std::move(layer));
        m_active = m_layers.size() - 1;
    }

    [[nodiscard]] Layer& at(std::size_t index) { return m_layers[index]; }
    [[nodiscard]] Layer& active() { return at(m_active); }
    [[nodiscard]] const Layer& active() const { return m_layers[m_active]; }
    [[nodiscard]] std::size_t active_index() const noexcept { return m_active; }
    [[nodiscard]] std::span<const Layer> layers() const noexcept { return m_layers; }

private:
    std::vector<Layer> m_layers;
    std::size_t m_active = 0;
};

}  // namespace lsp
