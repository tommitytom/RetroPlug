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
				area.x,
				area.y,
				border.left,
				area.h
			}, color);
		}

		if (border.top != 0.0f) {
			canvas.fillRect(RectF{
				area.x,
				area.y,
				area.w,
				border.top
			}, color);
		}

		if (border.right != 0.0f) {
			canvas.fillRect(RectF{
				area.right() - border.right,
				area.y,
				border.right,
				area.h
			}, color);
		}

		if (border.bottom != 0.0f) {
			canvas.fillRect(RectF{
				area.x,
				area.bottom() - border.bottom,
				area.w,
				border.bottom
			}, color);
		}
	}

	void renderElement(fw::Canvas& canvas, Document& doc, const DomElementHandle elementHandle) {
		entt::registry& reg = doc.getRegistry();
		const StyleHandle styleHandle = reg.get<StyleReferences>(elementHandle).current;

		DomStyle style = doc.getStyle(elementHandle);
		FlexBorder border = style.getBorder();
		if (std::isnan(border.top)) { border.top = 0.0f; }
		if (std::isnan(border.left)) { border.left = 0.0f; }
		if (std::isnan(border.bottom)) { border.bottom = 0.0f; }
		if (std::isnan(border.right)) { border.right = 0.0f; }

		const RectF& area = reg.get<WorldAreaComponent>(elementHandle).area;

		const styles::BackgroundColor* bg = reg.try_get<styles::BackgroundColor>(styleHandle);
		if (bg) {
			canvas.fillRect(area, bg->value);
		}

		const styles::Color* fg = reg.try_get<styles::Color>(styleHandle);
		Color4F borderColor = fg ? fg->value : Color4F::black;

		canvas.strokeRect(StrokedRect{
			.area = area,
			.width = BorderWidth {
				.top = border.top,
				.left = border.left,
				.bottom = border.bottom,
				.right= border.right
			},
			.color = {
				.top = borderColor,
				.left = borderColor,
				.bottom = borderColor,
				.right = borderColor
			}
		});

		//canvas.strokeRect(area, Color4F::white);

		const TextureComponent* texture = reg.try_get<TextureComponent>(elementHandle);
		if (texture) {
			canvas.texture(texture->texture, area);
		}

		const TextComponent* text = reg.try_get<TextComponent>(elementHandle);
		if (text) {
			const FontFaceStyle* face = reg.try_get<FontFaceStyle>(styleHandle);
			
			if (face && face->handle.isValid()) {
				FlexBorder padding = style.getComputedPadding();
				RectF textArea(area.x + padding.left, area.y + padding.top, area.w - padding.left - padding.right, area.h - padding.top - padding.bottom);
				canvas.setFont(face->handle);
				canvas.text(textArea, text->text, fg ? fg->value : Color4F::white);
			}
		}

		DocumentUtil::each(reg, elementHandle, [&](entt::entity child) {
			renderElement(canvas, doc, child);
		});
	}
	
	void DocumentRenderer::render(fw::Canvas& canvas, fw::Document& document) {
		renderElement(canvas, document, document.getRootElement());
	}
}
