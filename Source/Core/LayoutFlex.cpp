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
#include "../../Include/RmlUi/Core/Element.h"
#include "../../Include/RmlUi/Core/Types.h"
#include <algorithm>
#include <numeric>

namespace Rml {

Vector2f LayoutFlex::Format(Box& box, Vector2f /*min_size*/, Vector2f /*max_size*/, Element* element_flex)
{
	const ComputedValues& computed_flex = element_flex->GetComputedValues();

	// Scrollbars are illegal in the table element.
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

	Math::SnapToPixelGrid(flex_content_offset, flex_available_content_size);

	const Vector2f table_gap = Vector2f(
		ResolveValue(computed_flex.column_gap, flex_available_content_size.x), // TODO: Fix for infinite values
		ResolveValue(computed_flex.row_gap, flex_available_content_size.y)
	);

	// Construct the layout object and format the table.
	LayoutFlex layout_flex(element_flex, flex_available_content_size);

	layout_flex.Format();

	// Update the box size based on the new table size.
	box.SetContent(layout_flex.flex_resulting_content_size);

	return layout_flex.flex_content_overflow_size;
}


LayoutFlex::LayoutFlex(Element* element_flex, Vector2f flex_available_content_size)
	: element_flex(element_flex), flex_available_content_size(flex_available_content_size)
{}

void LayoutFlex::Format()
{

}


} // namespace Rml
