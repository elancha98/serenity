/*
 * Copyright (c) 2020, Andreas Kling <kling@serenityos.org>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Forward.h>
#include <LibWeb/Layout/FormattingContext.h>
#include <LibWeb/Layout/TableWrapper.h>

namespace Web::Layout {

class TableFormattingContext final : public FormattingContext {
public:
    explicit TableFormattingContext(LayoutState&, Box const&, FormattingContext* parent);
    ~TableFormattingContext();

    virtual void run(Box const&, LayoutMode, AvailableSpace const&) override;
    virtual CSSPixels automatic_content_width() const override;
    virtual CSSPixels automatic_content_height() const override;

    Box const& table_box() const { return context_box(); }
    TableWrapper const& table_wrapper() const
    {
        return verify_cast<TableWrapper>(*table_box().containing_block());
    }

private:
    void calculate_row_column_grid(Box const&);
    void compute_table_measures();
    void compute_table_width();
    void distribute_width_to_columns();
    void determine_intrisic_size_of_table_container(AvailableSpace const& available_space);
    void compute_table_height(LayoutMode layout_mode);
    void distribute_height_to_rows();
    void position_row_boxes(CSSPixels&);
    void position_cell_boxes();

    CSSPixels m_table_height { 0 };
    CSSPixels m_automatic_content_height { 0 };

    Optional<AvailableSpace> m_available_space;

    enum class ColumnType {
        Percent,
        Pixel,
        Auto
    };

    struct Column {
        ColumnType type { ColumnType::Auto };
        CSSPixels left_offset { 0 };
        CSSPixels min_width { 0 };
        CSSPixels max_width { 0 };
        CSSPixels used_width { 0 };
        double percentage_width { 0 };
    };

    struct Row {
        JS::NonnullGCPtr<Box const> box;
        CSSPixels base_height { 0 };
        CSSPixels reference_height { 0 };
        CSSPixels final_height { 0 };
        CSSPixels baseline { 0 };
    };

    struct Cell {
        JS::NonnullGCPtr<Box const> box;
        size_t column_index;
        size_t row_index;
        size_t column_span;
        size_t row_span;
        CSSPixels baseline { 0 };
        CSSPixels min_width { 0 };
        CSSPixels max_width { 0 };
    };

    CSSPixels compute_row_content_height(Cell const& cell) const;

    Vector<Cell> m_cells;
    Vector<Column> m_columns;
    Vector<Row> m_rows;
};

}
