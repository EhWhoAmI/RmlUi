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



Vector2f LayoutFlex::Format(Box& box, Vector2f /*min_size*/, Vector2f /*max_size*/, const Vector2f flex_containing_block, Element* element_flex)
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
	LayoutFlex layout_flex(element_flex, flex_available_content_size, flex_content_containing_block, flex_content_offset);

	layout_flex.Format();

	// Update the box size based on the new table size.
	box.SetContent(layout_flex.flex_resulting_content_size);

	return layout_flex.flex_content_overflow_size;
}


LayoutFlex::LayoutFlex(Element* element_flex, Vector2f flex_available_content_size, Vector2f flex_content_containing_block, Vector2f flex_content_offset)
	: element_flex(element_flex), flex_available_content_size(flex_available_content_size), flex_content_containing_block(flex_content_containing_block), flex_content_offset(flex_content_offset)
{}


// Seems we can share this from table layouting. TODO: Generalize original definition.
using ComputedFlexItemSize = ComputedTrackSize;

struct FlexItem {
	Element* element;

	bool auto_margin_a, auto_margin_b;
	float margin_a, margin_b;
	float padding_border_a, padding_border_b;

	float flex_base_size;         // Inner size
	float hypothetical_main_size; // Outer size

};

struct FlexLine {
	Vector<FlexItem> items;
	float accumulated_hypothetical_main_size;
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

void LayoutFlex::Format()
{
	const ComputedValues& computed_flex = element_flex->GetComputedValues();
	const Style::FlexDirection direction = computed_flex.flex_direction;
	const bool main_axis_horizontal = (direction == Style::FlexDirection::Row || direction == Style::FlexDirection::RowReverse);

	const float main_available_size = (main_axis_horizontal ? flex_available_content_size.x : flex_available_content_size.y);

	// For the purpose of placing items we make infinite size a big value.
	const float main_size = (main_available_size < 0.0f ? FLT_MAX : main_available_size);

	// For the purpose of resolving lengths, infinite main size becomes zero.
	const float main_size_base_value = (main_available_size < 0.0f ? 0.0f : main_available_size);

	// Build a list of all flex items with base size information.
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

		// TODO: The Build... names have reverse meaning from table layout
		ComputedFlexItemSize item_size = main_axis_horizontal ? BuildComputedColumnSize(computed) : BuildComputedRowSize(computed);

		item.auto_margin_a = (item_size.margin_a.type == Style::Margin::Auto);
		item.auto_margin_b = (item_size.margin_b.type == Style::Margin::Auto);

		GetEdgeSizes(item.margin_a, item.margin_b, item.padding_border_a, item.padding_border_b, item_size, main_size_base_value);
		
		const float padding_border_sum = item.padding_border_a + item.padding_border_b;

		// Find the flex base size (possibly negative when using border box sizing)
		if (computed.flex_basis.type != Style::FlexBasis::Auto)
		{
			item.flex_base_size = ResolveValue(computed.flex_basis, main_size_base_value);
			if (computed.box_sizing == Style::BoxSizing::BorderBox)
				item.flex_base_size -= padding_border_sum;
		}
		else if (item_size.size.type != Style::LengthPercentageAuto::Auto)
		{
			item.flex_base_size = ResolveValue(item_size.size, main_size_base_value);
			if (computed.box_sizing == Style::BoxSizing::BorderBox)
				item.flex_base_size -= padding_border_sum;
		}
		else if (main_axis_horizontal)
		{
			item.flex_base_size = LayoutDetails::GetShrinkToFitWidth(element, flex_content_containing_block);
		}
		else
		{
			LayoutEngine::FormatElement(element, flex_content_containing_block);
			item.flex_base_size = element->GetBox().GetSize().y;
		}

		// Calculate the hypothetical main size (clamped flex base size).
		{
			float min_size = ResolveValue(item_size.min_size, main_size_base_value);
			float max_size = (item_size.max_size.value < 0.f ? FLT_MAX : ResolveValue(item_size.max_size, main_size_base_value));

			if (computed.box_sizing == Style::BoxSizing::BorderBox)
			{
				min_size = Math::Max(0.0f, min_size - padding_border_sum);
				if (max_size < FLT_MAX)
					max_size = Math::Max(0.0f, max_size - padding_border_sum);
			}

			item.hypothetical_main_size = Math::Clamp(item.flex_base_size, min_size, max_size) + padding_border_sum;
		}

		items.push_back(std::move(item));
	}

	if (items.empty())
	{
		return;
	}


	// Collect the items into lines.
	FlexContainer container;

	if (computed_flex.flex_wrap == Style::FlexWrap::Nowrap)
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

			if (!line_items.empty() && cursor > main_size)
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
	// If the available main size is infinite, the main size becomes the accumulated outer size of all items of the widest line.
	const float main_size_definite =
		main_available_size >= 0.f ? main_available_size :
		std::max_element(container.lines.begin(), container.lines.end(), [](const FlexLine& a, const FlexLine& b) {
			return a.accumulated_hypothetical_main_size < b.accumulated_hypothetical_main_size;
		})->accumulated_hypothetical_main_size;


	float cursor_cross_axis = 0.f;

	// Resolve flexible lengths
	for (FlexLine& line : container.lines)
	{
		const float available_flex_space = Math::Max(0.0f, main_size_definite - line.accumulated_hypothetical_main_size);
		const float flex_space_per_item = available_flex_space / float(line.items.size());

		float cursor_main_axis = 0.f;
		float max_cross_size = 0;

		for (auto& item : line.items)
		{
			Box box;
			LayoutDetails::BuildBox(box, flex_content_containing_block, item.element, false, 0.f);

			float item_main_size = item.hypothetical_main_size - item.padding_border_a - item.padding_border_b + flex_space_per_item;
			float item_main_offset = cursor_main_axis + box.GetEdge(Box::MARGIN, main_axis_horizontal ? Box::LEFT : Box::TOP);
			Math::SnapToPixelGrid(item_main_offset, item_main_size);

			box.SetContent(main_axis_horizontal ? Vector2f(item_main_size, box.GetSize().y) : Vector2f(box.GetSize().x, item_main_size));

			const float item_cross_offset = Math::RoundFloat(cursor_cross_axis + box.GetEdge(Box::MARGIN, main_axis_horizontal ? Box::TOP : Box::LEFT));
			const Vector2f item_offset = main_axis_horizontal ? Vector2f(item_main_offset, item_cross_offset) : Vector2f(item_cross_offset, item_main_offset);

			Vector2f cell_visible_overflow_size;
			LayoutEngine::FormatElement(item.element, flex_content_containing_block, &box, &cell_visible_overflow_size);

			// Set the position of the element within the the flex container
			item.element->SetOffset(flex_content_offset + item_offset, element_flex);

			// The cell contents may overflow, propagate this to the flex container.
			flex_content_overflow_size.x = Math::Max(flex_content_overflow_size.x, item_offset.x + cell_visible_overflow_size.x);
			flex_content_overflow_size.y = Math::Max(flex_content_overflow_size.y, item_offset.y + cell_visible_overflow_size.y);

			max_cross_size = Math::Max(max_cross_size, item.element->GetBox().GetSizeAcross(Box::VERTICAL, Box::MARGIN));
			cursor_main_axis += item.hypothetical_main_size + flex_space_per_item;
		}

		cursor_cross_axis += max_cross_size;
	}

	const float cross_size_definite = cursor_cross_axis;

	flex_resulting_content_size = Vector2f(main_size_definite, cross_size_definite);
	if (!main_axis_horizontal)
		std::swap(flex_resulting_content_size.x, flex_resulting_content_size.y);

}


} // namespace Rml
