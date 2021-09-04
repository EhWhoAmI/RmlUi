/*
 * This source file is part of RmlUi, the HTML/CSS Interface Middleware
 *
 * For the latest information, see http://github.com/mikke89/RmlUi
 *
 * Copyright (c) 2008-2010 CodePoint Ltd, Shift Technology Ltd
 * Copyright (c) 2019 The RmlUi Team, and contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "LayoutFlex.h"
#include "LayoutDetails.h"
#include "LayoutEngine.h"
#include "LayoutTableDetails.h" // TODO
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/Types.h"
#include <algorithm>
#include <numeric>

namespace Rml {



Vector2f LayoutFlex::Format(Box& box, const Vector2f min_size, const Vector2f max_size, const Vector2f flex_containing_block, Element* element_flex)
{
	const ComputedValues& computed_flex = element_flex->GetComputedValues();

	if (!(computed_flex.overflow_x == Style::Overflow::Visible || computed_flex.overflow_x == Style::Overflow::Hidden) ||
		!(computed_flex.overflow_y == Style::Overflow::Visible || computed_flex.overflow_y == Style::Overflow::Hidden))
	{
		Log::Message(Log::LT_WARNING, "Scrolling flexboxes not yet implemented: %s.", element_flex->GetAddress().c_str());
		return Vector2f(0);
	}

	const Vector2f box_content_size = box.GetSize();
	const bool table_auto_height = (box_content_size.y < 0.0f);

	Vector2f flex_content_offset = box.GetPosition();
	Vector2f flex_available_content_size = Vector2f(box_content_size.x, box_content_size.y); // May be negative for infinite space

	Vector2f flex_content_containing_block = flex_available_content_size;
	if (flex_content_containing_block.y < .0f)
		flex_content_containing_block.y = flex_containing_block.y;

	Math::SnapToPixelGrid(flex_content_offset, flex_available_content_size);

	const Vector2f table_gap = Vector2f(
		ResolveValue(computed_flex.column_gap, flex_available_content_size.x), // TODO: Fix for infinite values
		ResolveValue(computed_flex.row_gap, flex_available_content_size.y)
	);

	// Construct the layout object and format the table.
	LayoutFlex layout_flex(element_flex, flex_available_content_size, flex_content_containing_block, flex_content_offset, min_size, max_size);

	layout_flex.Format();

	// Update the box size based on the new table size.
	box.SetContent(layout_flex.flex_resulting_content_size);

	return layout_flex.flex_content_overflow_size;
}


LayoutFlex::LayoutFlex(Element* element_flex, Vector2f flex_available_content_size, Vector2f flex_content_containing_block, Vector2f flex_content_offset,
	Vector2f flex_min_size, Vector2f flex_max_size)
	: element_flex(element_flex), flex_available_content_size(flex_available_content_size), flex_content_containing_block(flex_content_containing_block), flex_content_offset(flex_content_offset),
	flex_min_size(flex_min_size), flex_max_size(flex_max_size)
{}

// Seems we can share this from table layouting. TODO: Generalize original definition.
using ComputedFlexItemSize = ComputedTrackSize;

struct FlexItem {
	struct Size {
		bool auto_margin_a, auto_margin_b;
		bool auto_size;
		float margin_a, margin_b;
		float sum_edges;           // Inner->outer size
		float min_size, max_size;  // Inner size
	};

	Element* element;

	// Filled during the build step.
	Size main;
	Size cross;
	float flex_shrink_factor;
	float flex_grow_factor;
	Style::AlignSelf align_self;  // 'Auto' is replaced by container's 'align-items' value

	float inner_flex_base_size;   // Inner size
	float flex_base_size;         // Outer size
	float hypothetical_main_size; // Outer size

	// Used for resolving flexible length
	enum class Violation : std::uint8_t { None = 0, Min, Max };
	bool frozen;
	Violation violation;
	float target_main_size;       // Outer size
	float used_main_size;         // Outer size (without auto margins)
	float main_auto_margin_size_a, main_auto_margin_size_b;
	float main_offset;

	// Used for resolving cross size
	float hypothetical_cross_size;  // Outer size
	float used_cross_size;          // Outer size
	float cross_offset;             // Offset within line
};

struct FlexLine {
	Vector<FlexItem> items;
	float accumulated_hypothetical_main_size;
	float cross_size;
	float cross_spacing_a, cross_spacing_b;
	float cross_offset;
};

struct FlexContainer {
	Vector<FlexLine> lines;
};

static void GetEdgeSizes(float& margin_a, float& margin_b, float& padding_border_a, float& padding_border_b, const ComputedFlexItemSize& computed_size, const float base_value)
{
	// Todo: Copy/Pasted from TableDetails
	margin_a = ResolveValue(computed_size.margin_a, base_value);
	margin_b = ResolveValue(computed_size.margin_b, base_value);

	padding_border_a = Math::Max(0.0f, ResolveValue(computed_size.padding_a, base_value)) + Math::Max(0.0f, computed_size.border_a);
	padding_border_b = Math::Max(0.0f, ResolveValue(computed_size.padding_b, base_value)) + Math::Max(0.0f, computed_size.border_b);
}

static void GetItemSizing(FlexItem::Size& destination, const ComputedFlexItemSize& computed_size, const float base_value, const bool direction_reverse)
{
	float margin_a, margin_b, padding_border_a, padding_border_b;
	GetEdgeSizes(margin_a, margin_b, padding_border_a, padding_border_b, computed_size, base_value);

	const float padding_border = padding_border_a + padding_border_b;
	const float margin = margin_a + margin_b;

	destination.auto_margin_a = (computed_size.margin_a.type == Style::Margin::Auto);
	destination.auto_margin_b = (computed_size.margin_b.type == Style::Margin::Auto);

	destination.auto_size = (computed_size.size.type == Style::LengthPercentageAuto::Auto);

	destination.margin_a = margin_a;
	destination.margin_b = margin_b;
	destination.sum_edges = padding_border + margin;

	destination.min_size = ResolveValue(computed_size.min_size, base_value);
	destination.max_size = (computed_size.max_size.value < 0.f ? FLT_MAX : ResolveValue(computed_size.max_size, base_value));

	if (computed_size.box_sizing == Style::BoxSizing::BorderBox)
	{
		destination.min_size = Math::Max(0.0f, destination.min_size - padding_border);
		if (destination.max_size < FLT_MAX)
			destination.max_size = Math::Max(0.0f, destination.max_size - padding_border);
	}

	if (direction_reverse)
	{
		std::swap(destination.auto_margin_a, destination.auto_margin_b);
		std::swap(destination.margin_a, destination.margin_b);
	}
}

void LayoutFlex::Format()
{
	const ComputedValues& computed_flex = element_flex->GetComputedValues();
	const Style::FlexDirection direction = computed_flex.flex_direction;

	const bool main_axis_horizontal = (direction == Style::FlexDirection::Row || direction == Style::FlexDirection::RowReverse);
	const bool direction_reverse = (direction == Style::FlexDirection::RowReverse || direction == Style::FlexDirection::ColumnReverse);
	const bool flex_single_line = (computed_flex.flex_wrap == Style::FlexWrap::Nowrap);
	const bool wrap_reverse = (computed_flex.flex_wrap == Style::FlexWrap::WrapReverse);

	const float main_available_size = (main_axis_horizontal ? flex_available_content_size.x : flex_available_content_size.y);
	const float cross_available_size = (!main_axis_horizontal ? flex_available_content_size.x : flex_available_content_size.y);

	const float main_min_size = (main_axis_horizontal ? flex_min_size.x : flex_min_size.y);
	const float main_max_size = (main_axis_horizontal ? flex_max_size.x : flex_max_size.y);
	const float cross_min_size = (main_axis_horizontal ? flex_min_size.y : flex_min_size.x);
	const float cross_max_size = (main_axis_horizontal ? flex_max_size.y : flex_max_size.x);

	// For the purpose of placing items we make infinite size a big value.
	const float main_wrap_size = Math::Clamp(main_available_size < 0.0f ? FLT_MAX : main_available_size, main_min_size, main_max_size);

	// For the purpose of resolving lengths, infinite main size becomes zero.
	const float main_size_base_value = (main_available_size < 0.0f ? 0.0f : main_available_size);
	const float cross_size_base_value = (cross_available_size < 0.0f ? 0.0f : cross_available_size);

	// -- Build a list of all flex items with base size information --
	Vector<FlexItem> items;

	const int num_flex_children = element_flex->GetNumChildren();
	for (int i = 0; i < num_flex_children; i++)
	{
		Element* element = element_flex->GetChild(i);
		const ComputedValues& computed = element->GetComputedValues();

		if (computed.display == Style::Display::None)
		{
			continue;
		}
		else if (computed.position == Style::Position::Absolute || computed.position == Style::Position::Fixed)
		{
			// TODO: Absolutely positioned item
			continue;
		}

		FlexItem item = {};
		item.element = element;

		Style::LengthPercentageAuto item_main_size;

		{
			// TODO: The Build... names have reverse meaning from table layout
			ComputedFlexItemSize computed_main_size = main_axis_horizontal ? BuildComputedColumnSize(computed) : BuildComputedRowSize(computed);
			// TODO: Not very efficient.
			ComputedFlexItemSize computed_cross_size = !main_axis_horizontal ? BuildComputedColumnSize(computed) : BuildComputedRowSize(computed);

			GetItemSizing(item.main, computed_main_size, main_size_base_value, direction_reverse);
			GetItemSizing(item.cross, computed_cross_size, cross_size_base_value, wrap_reverse);

			item_main_size = computed_main_size.size;
		}

		item.flex_shrink_factor = computed.flex_shrink;
		item.flex_grow_factor = computed.flex_grow;
		item.align_self = computed.align_self;

		static_assert(int(Style::AlignSelf::FlexStart) == int(Style::AlignItems::FlexStart) + 1 &&
						  int(Style::AlignSelf::Stretch) == int(Style::AlignItems::Stretch) + 1,
			"It is assumed below that align items is a shifted version (no auto value) of align self.");

		// Use the container's align-items property if align-self is auto.
		if (item.align_self == Style::AlignSelf::Auto)
			item.align_self = static_cast<Style::AlignSelf>(static_cast<int>(computed_flex.align_items) + 1);

		const float sum_padding_border = item.main.sum_edges - (item.main.margin_a + item.main.margin_b);

		// Find the flex base size (possibly negative when using border box sizing)
		if (computed.flex_basis.type != Style::FlexBasis::Auto)
		{
			item.inner_flex_base_size = ResolveValue(computed.flex_basis, main_size_base_value);
			if (computed.box_sizing == Style::BoxSizing::BorderBox)
				item.inner_flex_base_size -= sum_padding_border;
		}
		else if (!item.main.auto_size)
		{
			item.inner_flex_base_size = ResolveValue(item_main_size, main_size_base_value);
			if (computed.box_sizing == Style::BoxSizing::BorderBox)
				item.inner_flex_base_size -= sum_padding_border;
		}
		else if (main_axis_horizontal)
		{
			item.inner_flex_base_size = LayoutDetails::GetShrinkToFitWidth(element, flex_content_containing_block);
		}
		else
		{
			Box box;
			LayoutDetails::BuildBox(box, flex_content_containing_block, element, false);
			if (box.GetSize().y >= 0.f)
			{
				item.inner_flex_base_size = box.GetSize().y;
			}
			else
			{
				LayoutEngine::FormatElement(element, flex_content_containing_block, &box);
				item.inner_flex_base_size = element->GetBox().GetSize().y;
			}
		}

		// Calculate the hypothetical main size (clamped flex base size).
		{
			item.hypothetical_main_size = Math::Clamp(item.inner_flex_base_size, item.main.min_size, item.main.max_size) + item.main.sum_edges;
			item.flex_base_size = item.inner_flex_base_size + item.main.sum_edges;
		}

		items.push_back(std::move(item));
	}

	if (items.empty())
	{
		return;
	}

	// -- Collect the items into lines --
	FlexContainer container;

	if (flex_single_line)
	{
		container.lines.push_back(FlexLine{ std::move(items) });
	}
	else
	{
		float cursor = 0;

		Vector<FlexItem> line_items;

		for (FlexItem& item : items)
		{
			cursor += item.hypothetical_main_size;

			if (!line_items.empty() && cursor > main_wrap_size)
			{
				// Break into new line.
				container.lines.push_back(FlexLine{ std::move(line_items) });
				cursor = item.hypothetical_main_size;
				line_items = { std::move(item) };
			}
			else
			{
				// Add item to current line.
				line_items.push_back(std::move(item));
			}
		}

		if (!line_items.empty())
			container.lines.push_back(FlexLine{ std::move(line_items) });

		items.clear();
		items.shrink_to_fit();
	}

	for (FlexLine& line : container.lines)
	{
		line.accumulated_hypothetical_main_size = std::accumulate(line.items.begin(), line.items.end(), 0.0f, [](float value, const FlexItem& item) {
			return value + item.hypothetical_main_size;
		});
	}

	// If the available main size is infinite, the used main size becomes the accumulated outer size of all items of the widest line.
	const float used_main_size =
		main_available_size >= 0.f ? main_available_size :
		std::max_element(container.lines.begin(), container.lines.end(), [](const FlexLine& a, const FlexLine& b) {
			return a.accumulated_hypothetical_main_size < b.accumulated_hypothetical_main_size;
		})->accumulated_hypothetical_main_size;

	// -- Determine main size --
	// Resolve flexible lengths to find the used main size of all items.
	for (FlexLine& line : container.lines)
	{
		const float available_flex_space = used_main_size - line.accumulated_hypothetical_main_size; // Possibly negative

		const bool flex_mode_grow = (available_flex_space > 0.f);

		auto FlexFactor = [flex_mode_grow](const FlexItem& item) {
			return (flex_mode_grow ? item.flex_grow_factor : item.flex_shrink_factor);
		};

		// Initialize items and freeze inflexible items.
		for (FlexItem& item : line.items)
		{
			item.target_main_size = item.flex_base_size;

			if (FlexFactor(item) == 0.f 
				|| (flex_mode_grow && item.flex_base_size > item.hypothetical_main_size)
				|| (!flex_mode_grow && item.flex_base_size < item.hypothetical_main_size))
			{
				item.frozen = true;
				item.target_main_size = item.hypothetical_main_size;
			}
		}

		auto RemainingFreeSpace = [used_main_size, &line]() {
			return used_main_size - std::accumulate(line.items.begin(), line.items.end(), 0.f, [](float value, const FlexItem& item) {
				return value + (item.frozen ? item.target_main_size : item.flex_base_size);
			});
		};

		const float initial_free_space = RemainingFreeSpace();

		// Now iteratively distribute or shrink the size of all the items, until all the items are frozen.
		while (!std::all_of(line.items.begin(), line.items.end(), [](const FlexItem& item) { return item.frozen; }))
		{
			float remaining_free_space = RemainingFreeSpace();

			const float flex_factor_sum = std::accumulate(line.items.begin(), line.items.end(), 0.f, [&FlexFactor](float value, const FlexItem& item) {
				return value + (item.frozen ? 0.0f : FlexFactor(item));
			});

			if (flex_factor_sum < 1.f)
			{
				const float scaled_initial_free_space = initial_free_space * flex_factor_sum;
				if (Math::AbsoluteValue(scaled_initial_free_space) < Math::AbsoluteValue(remaining_free_space))
					remaining_free_space = scaled_initial_free_space;
			}

			if (remaining_free_space != 0.f)
			{
				// Distribute free space proportionally to flex factors
				if (flex_mode_grow)
				{
					for (auto& item : line.items)
					{
						if (!item.frozen)
						{
							const float distribute_ratio = item.flex_grow_factor / flex_factor_sum;
							item.target_main_size = item.flex_base_size + distribute_ratio * remaining_free_space;
						}
					}
				}
				else
				{
					const float scaled_flex_shrink_factor_sum = std::accumulate(line.items.begin(), line.items.end(), 0.f, [](float value, const FlexItem& item) {
						return value + (item.frozen ? 0.0f : item.flex_shrink_factor * item.inner_flex_base_size);
					});

					for (auto& item : line.items)
					{
						if (!item.frozen)
						{
							const float scaled_flex_shrink_factor = item.flex_shrink_factor * item.inner_flex_base_size;
							const float distribute_ratio = scaled_flex_shrink_factor / scaled_flex_shrink_factor_sum;
							item.target_main_size = item.flex_base_size - distribute_ratio * Math::AbsoluteValue(remaining_free_space);
						}
					}
				}
			}

			// Clamp min/max violations
			float total_minmax_violation = 0.f;

			for (FlexItem& item : line.items)
			{
				if (!item.frozen)
				{
					const float inner_target_main_size = Math::Max(0.0f, item.target_main_size - item.main.sum_edges);
					const float clamped_target_main_size = Math::Clamp(inner_target_main_size, item.main.min_size, item.main.max_size) + item.main.sum_edges;

					const float violation_diff = clamped_target_main_size - item.target_main_size;
					item.violation = (violation_diff > 0.0f ? FlexItem::Violation::Min : (violation_diff < 0.f ? FlexItem::Violation::Max : FlexItem::Violation::None));
					item.target_main_size = clamped_target_main_size;
					
					total_minmax_violation += violation_diff;
				}
			}

			for (FlexItem& item : line.items)
			{
				 if (total_minmax_violation > 0.0f)
					item.frozen |= (item.violation == FlexItem::Violation::Min);
				else if (total_minmax_violation < 0.0f)
					item.frozen |= (item.violation == FlexItem::Violation::Max);
				else
					item.frozen = true;
			}
		}

		// Now, each item's used main size is found!
		for (FlexItem& item : line.items)
			item.used_main_size = item.target_main_size;
	}


	// -- Align main axis (§9.5) -- 
	// Main alignment is done before cross sizing. Due to rounding to the pixel grid, the main size can
	// change slightly after main alignment/offseting. Also, the cross sizing depends on the main sizing
	// so doing it in this order ensures no surprises (overflow/wrapping issues) due to pixel rounding.
	for (FlexLine& line : container.lines)
	{
		const float remaining_free_space = used_main_size - std::accumulate(line.items.begin(), line.items.end(), 0.f, [](float value, const FlexItem& item) {
			return value + item.used_main_size;
		});

		if (remaining_free_space > 0.0f)
		{
			const int num_auto_margins = std::accumulate(line.items.begin(), line.items.end(), 0, [](int value, const FlexItem& item) {
				return value + int(item.main.auto_margin_a) + int(item.main.auto_margin_b);
			});

			if (num_auto_margins > 0)
			{
				// Distribute the remaining space to the auto margins.
				const float space_per_auto_margin = remaining_free_space / float(num_auto_margins);
				for (FlexItem& item : line.items)
				{
					if (item.main.auto_margin_a)
						item.main_auto_margin_size_a = space_per_auto_margin;
					if (item.main.auto_margin_b)
						item.main_auto_margin_size_b = space_per_auto_margin;
				}
			}
			else
			{
				// Distribute the remaining space based on the 'justify-content' property.
				using Style::JustifyContent;
				const int num_items = int(line.items.size());

				switch (computed_flex.justify_content)
				{
				case JustifyContent::SpaceBetween:
					if (num_items > 1)
					{
						const float space_per_edge = remaining_free_space / float(2 * num_items - 2);
						for (int i = 0; i < num_items; i++)
						{
							FlexItem& item = line.items[i];
							if (i > 0)
								item.main_auto_margin_size_a = space_per_edge;
							if (i < num_items - 1)
								item.main_auto_margin_size_b = space_per_edge;
						}
						break;
					}
					//-fallthrough
				case JustifyContent::FlexStart:
					line.items.back().main_auto_margin_size_b = remaining_free_space;
					break;
				case JustifyContent::FlexEnd:
					line.items.front().main_auto_margin_size_a = remaining_free_space;
					break;
				case JustifyContent::Center:
					line.items.front().main_auto_margin_size_a = 0.5f * remaining_free_space;
					line.items.back().main_auto_margin_size_b = 0.5f * remaining_free_space;
					break;
				case JustifyContent::SpaceAround:
				{
					const float space_per_edge = remaining_free_space / float(2 * num_items);
					for (FlexItem& item : line.items)
					{
						item.main_auto_margin_size_a = space_per_edge;
						item.main_auto_margin_size_b = space_per_edge;
					}
				}
				break;
				}
			}
		}

		// Now find the offset and snap the outer edges to the pixel grid.
		const float reverse_offset = used_main_size - line.items[0].used_main_size + line.items[0].main.margin_a + line.items[0].main.margin_b;
		float cursor = 0.0f;
		for (FlexItem& item : line.items)
		{
			item.main_offset = cursor + item.main.margin_a + item.main_auto_margin_size_a;
			cursor += item.used_main_size + item.main_auto_margin_size_a + item.main_auto_margin_size_b;

			if (direction_reverse)
				item.main_offset = reverse_offset - item.main_offset;

			Math::SnapToPixelGrid(item.main_offset, item.used_main_size);
		}
	}


	// -- Determine cross size (§9.4) --
	// First, determine the cross size of each item, format it if necessary.
	for (FlexLine& line : container.lines)
	{
		for (FlexItem& item : line.items)
		{
			// TODO: Maybe move this simultaneously with main size determination
			Box box;
			LayoutDetails::BuildBox(box, flex_content_containing_block, item.element, false, 0.0f);
			const Vector2f content_size = box.GetSize();
			const float used_main_size_inner = item.used_main_size - item.main.sum_edges;

			if (main_axis_horizontal)
			{
				if (content_size.y < 0.0f)
				{
					box.SetContent(Vector2f(used_main_size_inner, content_size.y));
					LayoutEngine::FormatElement(item.element, flex_content_containing_block, &box);
					item.hypothetical_cross_size = item.element->GetBox().GetSize().y + item.cross.sum_edges;
				}
				else
				{
					item.hypothetical_cross_size = content_size.y + item.cross.sum_edges;
				}
			}
			else
			{
				if (content_size.x < 0.0f || item.cross.auto_size)
				{
					box.SetContent(Vector2f(content_size.x, used_main_size_inner));
					item.hypothetical_cross_size = LayoutDetails::GetShrinkToFitWidth(item.element, flex_content_containing_block) + item.cross.sum_edges;
				}
				else
				{
					item.hypothetical_cross_size = content_size.x + item.cross.sum_edges;
				}
			}
		}
	}

	// Determine cross size of each line.
	if (cross_available_size >= 0.f && flex_single_line && container.lines.size() == 1)
	{
		container.lines[0].cross_size = cross_available_size;
	}
	else
	{
		for (FlexLine& line : container.lines)
		{
			const float largest_hypothetical_cross_size = std::max_element(line.items.begin(), line.items.end(), [](const FlexItem& a, const FlexItem& b) {
				return a.hypothetical_cross_size < b.hypothetical_cross_size;
			})->hypothetical_cross_size;

			line.cross_size = Math::Max(0.0f, largest_hypothetical_cross_size);

			if (flex_single_line)
				line.cross_size = Math::Clamp(line.cross_size, cross_min_size, cross_max_size);
		}
	}

	// Stretch out the lines if we have extra space.
	if (cross_available_size >= 0.f && computed_flex.align_content == Style::AlignContent::Stretch)
	{
		const float remaining_space = cross_available_size - std::accumulate(container.lines.begin(), container.lines.end(), 0.f, [](float value, const FlexLine& line) {
			return value + line.cross_size;
		});

		if (remaining_space > 0.f)
		{
			const float add_space_per_line = remaining_space / float(container.lines.size());
			for (FlexLine& line : container.lines)
				line.cross_size += add_space_per_line;
		}
	}

	// Determine the used cross size of items.
	for (FlexLine& line : container.lines)
	{
		for (FlexItem& item : line.items)
		{
			const bool stretch_item = (item.align_self == Style::AlignSelf::Stretch);
			if (stretch_item && item.cross.auto_size && !item.cross.auto_margin_a && !item.cross.auto_margin_b)
			{
				item.used_cross_size = Math::Clamp(line.cross_size - item.cross.sum_edges, item.cross.min_size, item.cross.max_size) + item.cross.sum_edges;
				// Here we are supposed to re-format the item with the new size, so that percentages can be resolved, see CSS specs Sec. 9.4.11. Seems very slow, we skip this for now.
			}
			else
			{
				item.used_cross_size = item.hypothetical_cross_size;
			}
		}
	}

	// -- Align cross axis (§9.6) --
	for (FlexLine& line : container.lines)
	{
		for (FlexItem& item : line.items)
		{
			const float remaining_space = line.cross_size - item.used_cross_size;

			item.cross_offset = item.cross.margin_a;

			if (remaining_space > 0.f)
			{
				const int num_auto_margins = int(item.cross.auto_margin_a) + int(item.cross.auto_margin_b);
				if (num_auto_margins > 0)
				{
					const float space_per_auto_margin = remaining_space / float(num_auto_margins);
					item.cross_offset = item.cross.margin_a + (item.cross.auto_margin_a ? space_per_auto_margin : 0.f);
				}
				else
				{
					using Style::AlignSelf;
					const AlignSelf align_self = item.align_self;

					switch (align_self)
					{
					case AlignSelf::Auto:
						// Never encountered here: should already have been replaced by container's align-items property.
						RMLUI_ERROR;
						break;
					case AlignSelf::FlexStart:
						// Do nothing
						break;
					case AlignSelf::FlexEnd:
						item.cross_offset = item.cross.margin_a + remaining_space;
						break;
					case AlignSelf::Center:
						item.cross_offset = item.cross.margin_a + 0.5f * remaining_space;
						break;
					case AlignSelf::Baseline:
						Log::Message(Log::LT_WARNING, "Flexbox baseline not yet implemented");
						break;
					case AlignSelf::Stretch:
						// Handled above
						break;
					}
				}
			}

			if (wrap_reverse)
			{
				const float reverse_offset = line.cross_size - item.used_cross_size + item.cross.margin_a + item.cross.margin_b;
				item.cross_offset = reverse_offset - item.cross_offset;
			}
		}

		// Snap the outer item cross edges to the pixel grid.
		for (FlexItem& item : line.items)
			Math::SnapToPixelGrid(item.cross_offset, item.used_cross_size);
	}

	const float accumulated_lines_cross_size = std::accumulate(
		container.lines.begin(), container.lines.end(), 0.f, [](float value, const FlexLine& line) { return value + line.cross_size; });

	// If the available cross size is infinite, the used cross size becomes the accumulated line cross size.
	const float used_cross_size = cross_available_size >= 0.f ? cross_available_size : accumulated_lines_cross_size;

	// Align the lines along the cross-axis.
	{
		const float remaining_free_space = used_cross_size - accumulated_lines_cross_size;
		const int num_lines = int(container.lines.size());

		if (remaining_free_space > 0.f)
		{
			using Style::AlignContent;

			switch (computed_flex.align_content)
			{
			case AlignContent::SpaceBetween:
				if (num_lines > 1)
				{
					const float space_per_edge = remaining_free_space / float(2 * num_lines - 2);
					for (int i = 0; i < num_lines; i++)
					{
						FlexLine& line = container.lines[i];
						if (i > 0)
							line.cross_spacing_a = space_per_edge;
						if (i < num_lines - 1)
							line.cross_spacing_b = space_per_edge;
					}
				}
				//-fallthrough
			case AlignContent::FlexStart:
				container.lines.back().cross_spacing_b = remaining_free_space;
				break;
			case AlignContent::FlexEnd:
				container.lines.front().cross_spacing_a = remaining_free_space;
				break;
			case AlignContent::Center:
				container.lines.front().cross_spacing_a = 0.5f * remaining_free_space;
				container.lines.back().cross_spacing_b = 0.5f * remaining_free_space;
				break;
			case AlignContent::SpaceAround:
			{
				const float space_per_edge = remaining_free_space / float(2 * num_lines);
				for (FlexLine& line : container.lines)
				{
					line.cross_spacing_a = space_per_edge;
					line.cross_spacing_b = space_per_edge;
				}
			}
				break;
			case AlignContent::Stretch:
				// Handled above.
				break;
			}
		}

		// Now find the offset and snap the line edges to the pixel grid.
		const float reverse_offset = used_cross_size - container.lines[0].cross_size;
		float cursor = 0.f;
		for (FlexLine& line : container.lines)
		{
			line.cross_offset = cursor + line.cross_spacing_a;
			cursor = line.cross_offset + line.cross_size + line.cross_spacing_b;

			if (wrap_reverse)
				line.cross_offset = reverse_offset - line.cross_offset;

			Math::SnapToPixelGrid(line.cross_offset, line.cross_size);
		}
	}

	// -- Format items --
	for (const FlexLine& line : container.lines)
	{
		for (const FlexItem& item : line.items)
		{
			// TODO: Store box from earlier?
			Box box;
			LayoutDetails::BuildBox(box, flex_content_containing_block, item.element, false, 0.f);

			float item_main_size = item.used_main_size - item.main.sum_edges;
			float item_main_offset = item.main_offset;
			
			float item_cross_size = item.used_cross_size - item.cross.sum_edges;
			float item_cross_offset = line.cross_offset + item.cross_offset;

			box.SetContent(main_axis_horizontal ? Vector2f(item_main_size, item_cross_size) : Vector2f(item_cross_size, item_main_size));

			const Vector2f item_offset = main_axis_horizontal ? Vector2f(item_main_offset, item_cross_offset) : Vector2f(item_cross_offset, item_main_offset);

			Vector2f cell_visible_overflow_size;
			LayoutEngine::FormatElement(item.element, flex_content_containing_block, &box, &cell_visible_overflow_size);

			// Set the position of the element within the the flex container
			item.element->SetOffset(flex_content_offset + item_offset, element_flex);

			// The cell contents may overflow, propagate this to the flex container.
			flex_content_overflow_size.x = Math::Max(flex_content_overflow_size.x, item_offset.x + cell_visible_overflow_size.x);
			flex_content_overflow_size.y = Math::Max(flex_content_overflow_size.y, item_offset.y + cell_visible_overflow_size.y);
		}
	}

	flex_resulting_content_size = main_axis_horizontal ? Vector2f(used_main_size, used_cross_size) : Vector2f(used_cross_size, used_main_size);
}

} // namespace Rml
