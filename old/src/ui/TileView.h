#pragma once

#include "ui/View.h"

namespace rp {
	class TileGrid;

	class TileView : public orb::View {
		FwRegisterObject()
	private:
		entt::entity _entity;

	public:
		TileView(entt::entity entity): _entity(entity) {}
		~TileView() = default;

		entt::entity getEntity() const {
			return _entity;
		}

		void requestSelected();
	};
	using TileViewPtr = std::shared_ptr<TileView>;
}
