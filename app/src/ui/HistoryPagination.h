#pragma once
#include <algorithm>

namespace earlab::ui
{
/** View-independent paging state; preserves the current record on resize. */
class HistoryPagination
{
public:
    void setItemCount (int count) noexcept { items = std::max (0, count); clamp(); }
    void setRowsPerPage (int count) noexcept
    {
        const int first = firstVisible();
        rows = std::max (1, count);
        page = first / rows;
        clamp();
    }
    void goTo (int index) noexcept { page = index; clamp(); }
    void move (int delta) noexcept { goTo (page + delta); }
    [[nodiscard]] int currentPage() const noexcept { return page; }
    [[nodiscard]] int pageCount() const noexcept { return std::max (1, (items + rows - 1) / rows); }
    [[nodiscard]] int firstVisible() const noexcept { return page * rows; }
    [[nodiscard]] int visibleCount() const noexcept { return std::min (rows, items - firstVisible()); }
private:
    void clamp() noexcept { page = std::clamp (page, 0, pageCount() - 1); }
    int items = 0, rows = 1, page = 0;
};
} // namespace earlab::ui
