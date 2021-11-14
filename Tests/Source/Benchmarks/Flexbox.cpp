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

#include "../Common/TestsShell.h"
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Types.h>
#include <doctest.h>
#include <nanobench.h>

using namespace ankerl;
using namespace Rml;

static const String rml_flexbox_mixed_document = R"(
<rml>
<head>
    <title>Flex 02 - Various features</title>
    <link type="text/rcss" href="/../Tests/Data/style.rcss"/>
	<style>
        .flex-container {
            display: flex;
            margin: 10px 20px;
            background-color: #333;
            max-height: 210px;
            flex-wrap: wrap-reverse;
        }

        .flex-item {
            width: 50px;
            margin: 20px;
            background-color: #eee;
            height: 50px;
            text-align: center;
        }

        .flex-direction-row {
            flex-direction: row;
        }
        .flex-direction-row-reverse {
            flex-direction: row-reverse;
        }
        .flex-direction-column {
            flex-direction: column;
        }
        .flex-direction-column-reverse {
            flex-direction: column-reverse;
        }
        .absolute {
            margin: 0;
            position: absolute;
            right: 0;
            bottom: 10px;
        }
	</style>
</head>

<body>
</body>
</rml>
)";

static const String rml_flexbox_mixed_body = R"(
<div class="flex-container flex-direction-row" style="position: relative">
    <div class="flex-item absolute">Abs</div>
    <div class="flex-item" style="margin: 50px;">1</div>
    <div class="flex-item" style="margin-top: auto">2</div>
    <div class="flex-item" style="margin: auto">3</div>
</div>
<div class="flex-container flex-direction-row-reverse" style="height: 200px; justify-content: space-around;">
    <div class="flex-item">1</div>
    <div class="flex-item" style="margin-bottom: auto;">2</div>
    <div class="flex-item" style="margin-right: 40px;">3</div>
</div>
<div class="flex-container flex-direction-column">
    <div class="flex-item" id="test" style="margin-right: auto">1</div>
    <div class="flex-item">2</div>
    <div class="flex-item">3</div>
</div>
<div class="flex-container flex-direction-column-reverse">
    <div class="flex-item">1</div>
    <div class="flex-item">2 LONG_OVERFLOWING_WORD</div>
    <div class="flex-item">3</div>
</div>
)";

static const String rml_flexbox_scroll_document = R"(
<rml>
<head>
    <title>Flex 03 - Scrolling container</title>
    <link type="text/rcss" href="/../Tests/Data/style.rcss"/>
	<style>
		.flex {
			display: flex;
			background-color: #555;
			margin: 5dp 20dp 15dp;
			border: 2dp #333;
			justify-content: space-between;
			color: #d44fff;
		}
		.auto {
			overflow: auto;
		}
		.scroll {
			overflow: scroll;
		}
		.flex div {
			flex: 0 1 auto;
			width: 50dp;
			height: 50dp;
			margin: 20dp;
			background-color: #eee;
			line-height: 50dp;
			text-align: center;
		}
		.flex div.tall {
			height: 80dp;
			width: 15dp;
			margin: 0;
			border: 2dp #d44fff;
		}
	</style>
</head>
<body>
</body>
</rml>
)";

static const String rml_flexbox_scroll_body = R"(
overflow: scroll
<div class="flex scroll" id="scroll">
	<div>Hello<div class="tall"/></div>
	<div>big world!</div>
	<div>LOOOOOOOOOOOOOOOOOOOOONG</div>
</div>
overflow: auto
<div class="flex auto" id="auto">
	<div>Hello<div class="tall"/></div>
	<div>big world!</div>
	<div>LOOOOOOOOOOOOOOOOOOOOONG</div>
</div>
overflow: auto - only vertical overflow
<div class="flex auto" id="vertical">
	<div>Hello<div class="tall"/></div>
	<div>big world!</div>
	<div>LONG</div>
</div>
overflow: auto - only horizontal overflow
<div class="flex auto" id="horizontal">
	<div>Hello</div>
	<div>big</div>
	<div>LOOOOOOOOOOOOOOOOOOOOONG</div>
</div>
overflow: visible
<div class="flex" id="visible">
	<div>Hello<div class="tall"/></div>
	<div>big world!</div>
	<div>LOOOOOOOOOOOOOOOOOOOOONG</div>
</div>
)";

TEST_CASE("flexbox")
{
	Context* context = TestsShell::GetContext();
	REQUIRE(context);

	{
		nanobench::Bench bench;
		bench.title("Flexbox mixed");
		bench.relative(true);

		ElementDocument* document = context->LoadDocumentFromMemory(rml_flexbox_mixed_document);
		REQUIRE(document);
		document->Show();

		document->SetInnerRML(rml_flexbox_mixed_body);
		context->Update();
		context->Render();

		TestsShell::RenderLoop();

		bench.run("Update (unmodified)", [&] { context->Update(); });

		bench.run("Render", [&] { context->Render(); });

		bench.run("SetInnerRML", [&] { document->SetInnerRML(rml_flexbox_scroll_body); });

		bench.run("SetInnerRML + Update", [&] {
			document->SetInnerRML(rml_flexbox_mixed_body);
			context->Update();
		});

		bench.run("SetInnerRML + Update + Render", [&] {
			document->SetInnerRML(rml_flexbox_mixed_body);
			context->Update();
			context->Render();
		});

		document->Close();
	}

	{
		nanobench::Bench bench;
		bench.title("Flexbox scroll");
		bench.relative(true);

		ElementDocument* document = context->LoadDocumentFromMemory(rml_flexbox_scroll_document);
		REQUIRE(document);
		document->Show();

		document->SetInnerRML(rml_flexbox_scroll_body);
		context->Update();
		context->Render();

		TestsShell::RenderLoop();

		bench.run("Update (unmodified)", [&] { context->Update(); });

		bench.run("Render", [&] { context->Render(); });

		bench.run("SetInnerRML", [&] { document->SetInnerRML(rml_flexbox_scroll_body); });

		bench.run("SetInnerRML + Update", [&] {
			document->SetInnerRML(rml_flexbox_scroll_body);
			context->Update();
		});

		bench.run("SetInnerRML + Update + Render", [&] {
			document->SetInnerRML(rml_flexbox_scroll_body);
			context->Update();
			context->Render();
		});

		document->Close();
	}
}
