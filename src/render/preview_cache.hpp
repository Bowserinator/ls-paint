#pragma once

#include <chrono>
#include <compare>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

#include "config.hpp"
#include "render/tile_resample.hpp"

namespace lsp {

struct PreviewKey {
    int level = 0;
    int column = 0;
    int row = 0;
    auto operator<=>(const PreviewKey&) const = default;
};

// A square of the document halved `key.level` times, with a margin of the
// neighbouring pixels so that its edges filter without seams.
struct PreviewTile {
    PreviewKey key;
    // Changes whenever the pixels do.
    std::uint64_t revision = 0;
    // The document pixels the tile shows.
    RectI area;
    PixelGrid pixels;
};

// `clip` is the part of `tile` that shows the document area `area`. The
// bleed is how many more pixels of the tile, to the right and below, can be
// drawn under the next tile so that no seam shows between them.
struct PreviewPatch {
    RectI area;
    RectI clip;
    int bleed_x = 0;
    int bleed_y = 0;
    std::shared_ptr<const PreviewTile> tile;
};

// Pictures of the document at the sizes the view needs. Edits reach the
// pictures at once, so what is shown never trails the document.
class PreviewCache {
public:
    explicit PreviewCache(std::size_t budget = kPreviewCacheBytes) : m_budget(budget) {}

    void reset(RectI bounds, std::uint64_t generation);
    // Brings the tiles up to date with `source` where it changed.
    void invalidate(const PixelBuffer& source, RectI dirty);
    // Sets the part of the document on screen and its screen pixels per document pixel.
    void demand(RectI view, double scale);
    // Generates what the view needs for up to `budget`. True if work is left.
    bool build(const PixelBuffer& source, std::chrono::steady_clock::duration budget);
    // The tiles that cover the view. Finer tiles, or else a coarser one, stand
    // in for one that is not generated yet.
    [[nodiscard]] std::vector<PreviewPatch> plan() const;

    // Changes whenever the result of `plan` may have.
    [[nodiscard]] std::uint64_t version() const { return m_changes; }
    [[nodiscard]] std::size_t bytes() const;
    [[nodiscard]] std::size_t entry_count() const { return m_entries.size(); }
    [[nodiscard]] int level() const { return m_level; }
    [[nodiscard]] std::uint64_t generation() const { return m_generation; }
    [[nodiscard]] RectI bounds() const { return m_bounds; }
    [[nodiscard]] bool failed() const { return m_failed; }

private:
    struct Entry {
        std::shared_ptr<PreviewTile> tile;
        // The part of the tile, in its own pixels, that is yet to be generated.
        RectI stale;
        // Whether the tile has been generated in full once.
        bool complete = false;
        std::uint64_t used = 0;
    };

    [[nodiscard]] RectI core(PreviewKey key) const;
    [[nodiscard]] RectI samples(PreviewKey key) const;
    [[nodiscard]] RectI extent(PreviewKey key) const;
    Entry* ensure(PreviewKey key);
    bool make_room(std::size_t extra, PreviewKey keep, bool new_entry);
    void refresh(PreviewKey key, Entry& entry, RectI region, const PixelBuffer& source);
    bool advance(PreviewKey key, Entry& entry, const PixelBuffer& source);
    void add_patch(std::vector<PreviewPatch>& patches, RectI area, const std::shared_ptr<PreviewTile>& tile) const;

    RectI m_bounds;
    RectI m_view;
    PreviewKey m_overview;
    int m_level = 0;
    RectI m_asked_view;
    int m_asked_level = -1;
    std::uint64_t m_changes = 0;
    std::uint64_t m_generation = 0;
    std::uint64_t m_serial = 0;
    std::uint64_t m_clock = 0;
    // Entries used after this were asked for by the latest view.
    std::uint64_t m_pinned = 0;
    std::size_t m_budget;
    std::vector<PreviewKey> m_wanted;
    std::map<PreviewKey, Entry> m_entries;
    bool m_failed = false;
};

}  // namespace lsp
