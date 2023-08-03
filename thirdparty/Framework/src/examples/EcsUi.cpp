#include "EcsUi.h"

#include "foundation/LuaReflection.h"
#include "foundation/Input.h"
#include "ui/next/DocumentRenderer.h"
#include "ui/next/LuaReact.h"
#include "ui/next/StyleComponentsMeta.h"

namespace fw {
	EcsUi::EcsUi() : View({ 1024, 768 }) {
		setType<EcsUi>();
		getLayout().setDimensions(100_pc);
		setFocusPolicy(FocusPolicy::Click);
	}

	EcsUi::~EcsUi() {

	}
	
	void EcsUi::onInitialize() {
		_react = std::make_shared<LuaReact>(getFontManager(), "E:\\code\\RetroPlugNext\\thirdparty\\Framework\\src\\scripts\\react\\runner.lua");
	}

	/*bool EcsUi::propagateMouseClick(entt::entity e, MouseButtonEvent ev) {
		ViewLayout& node = *_reg->getNodeStyle(e);

		RectF area = node.getWorldArea();
		PointF pos = PointF(ev.position);

		if (area.contains(pos)) {
			if (emitEvent(e, "onClick", MouseButtonEvent{  })) {
				return true;
			}

			_reg->each(e, [&](entt::entity child) {
				propagateMouseMove(child, pos);
			});
		}

		return false;
	}*/
	

	bool EcsUi::onMouseMove(Point pos) {
		return _react->handleMouseMove(PointF(pos));
	}

	bool EcsUi::onKey(const KeyEvent& ev) {
		return false;
	}

	void EcsUi::onUpdate(f32 delta) {
		_react->update(delta);
	}
	
	void EcsUi::onRender(fw::Canvas& canvas) {
		Document& doc = _react->getDocument();
		doc.calculateLayout(DimensionF(canvas.getDimensions()));
		DocumentRenderer::render(canvas, doc);
	}

	void EcsUi::onHotReload() { 
		_react->reload(); 
	}
}
