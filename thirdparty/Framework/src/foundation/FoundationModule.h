#pragma once

#include <string>

#include "foundation/Math.h"
#include "foundation/MetaFactory.h"
#include "foundation/MetaUtil.h"

namespace fw::FoundationModule {
	void setup() {
		MetaUtil::registerType<bool>("bool");
		MetaUtil::registerType<int8>("int8");
		MetaUtil::registerType<int16>("int16");
		MetaUtil::registerType<int32>("int32");
		MetaUtil::registerType<int64>("int64");
		MetaUtil::registerType<uint8>("uint8");
		MetaUtil::registerType<uint16>("uint16");
		MetaUtil::registerType<uint32>("uint32");
		MetaUtil::registerType<uint64>("uint64");
		MetaUtil::registerType<f32>("f32");
		MetaUtil::registerType<f64>("f64");

		MetaUtil::registerType<std::string>("std::string");

		MetaFactory<Point>().addField<&Point::x>("x").addField<&Point::y>("y");
		MetaFactory<PointF>().addField<&PointF::x>("x").addField<&PointF::y>("y");

		MetaFactory<Dimension>().addField<&Dimension::w>("w").addField<&Dimension::h>("h");
		MetaFactory<DimensionF>().addField<&DimensionF::w>("w").addField<&DimensionF::h>("h");

		MetaFactory<Rect>().addField<&Rect::x>("x").addField<&Rect::y>("y").addField<&Rect::w>("w").addField<&Rect::h>("h");
		MetaFactory<RectF>().addField<&RectF::x>("x").addField<&RectF::y>("y").addField<&RectF::w>("w").addField<&RectF::h>("h");

		MetaFactory<Color4>().addField<&Color4::r>("r").addField<&Color4::g>("g").addField<&Color4::b>("b").addField<&Color4::a>("a");
		MetaFactory<Color4F>().addField<&Color4F::r>("r").addField<&Color4F::g>("g").addField<&Color4F::b>("b").addField<&Color4F::a>("a");
	}
}

