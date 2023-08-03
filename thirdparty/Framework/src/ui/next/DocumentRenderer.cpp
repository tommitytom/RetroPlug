#include "DocumentRenderer.h"

#include "foundation/Math.h"
#include "graphics/Canvas.h"
#include "ui/next/Document.h"
#include "ui/next/DocumentUtil.h"
#include "ui/next/DomStyle.h"
#include "ui/next/StyleComponents.h"

namespace fw {
	static void drawBorder(fw::Canvas& canvas, const RectF& area, const FlexBorder& border, Color4F color) {
		if (border.left != 0.0f) {
			canvas.fillRect(RectF{
				area.x - border.left,
				area.y - border.top,
				border.left,
				area.h + border.top + border.bottom
			}, color);
		}

		if (border.top != 0.0f) {
			canvas.fillRect(RectF{
				area.x - border.left,
				area.y - border.top,
				area.w + border.left + border.right,
				border.top
			}, color);
		}

		if (border.right != 0.0f) {
			canvas.fillRect(RectF{
				area.right(),
				area.y - border.top,
				border.right,
				area.h + border.top + border.bottom
			}, color);
		}

		if (border.bottom != 0.0f) {
			canvas.fillRect(RectF{
				area.x - border.left,
				area.bottom(),
				area.w + border.left + border.right,
				border.bottom
			}, color);
		}
	}

	void renderElement(fw::Canvas& canvas, Document& doc, DomElementHandle e) {
		entt::registry& reg = doc.getRegistry();

		DomStyle style = doc.getStyle(e);
		FlexBorder border = style.getBorder();
		if (std::isnan(border.top)) { border.top = 0.0f; }
		if (std::isnan(border.left)) { border.left = 0.0f; }
		if (std::isnan(border.bottom)) { border.bottom = 0.0f; }
		if (std::isnan(border.right)) { border.right = 0.0f; }

		const RectF& area = reg.get<WorldAreaComponent>(e).area;

		const BackgroundColorStyle* bg = reg.try_get<BackgroundColorStyle>(e);
		if (bg) {
			canvas.fillRect(area, bg->color);
		}

		const ColorStyle* fg = reg.try_get<ColorStyle>(e);

		drawBorder(canvas, area, border, Color4F::black);

		//canvas.strokeRect(area, Color4F::green);

		const TextureComponent* texture = reg.try_get<TextureComponent>(e);
		if (texture) {
			canvas.texture(texture->texture, area);
		}

		const TextComponent* text = reg.try_get<TextComponent>(e);
		if (text) {
			const FontFaceStyle* face = reg.try_get<FontFaceStyle>(e);
			
			if (face && face->handle.isValid()) {
				FlexBorder padding = style.getComputedPadding();
				RectF textArea(area.x + padding.left, area.y + padding.top, area.w - padding.left - padding.right, area.h - padding.top - padding.bottom);
				canvas.setFont(face->handle);
				canvas.text(textArea, text->text, fg ? fg->color : Color4F::white);
			}
		}

		DocumentUtil::each(reg, e, [&](entt::entity child) {
			renderElement(canvas, doc, child);
		});
	}
	
	void DocumentRenderer::render(fw::Canvas& canvas, fw::Document& document) {
		renderElement(canvas, document, document.getRootElement());
	}
}
