#pragma once

namespace fw {
	class Canvas;
	class Document;
}

namespace fw::DocumentRenderer {
	void render(fw::Canvas& canvas, fw::Document& document);
}
