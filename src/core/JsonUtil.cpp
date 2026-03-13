#include "JsonUtil.h"

namespace rp::JsonUtil {

	Writer::Writer(yyjson_mut_doc* _doc, yyjson_mut_val* _obj) : doc_(_doc), obj_(_obj) {}

	Writer::OutputArrayType Writer::array_as_root(const size_t) const noexcept {
		//assert(false);
		const auto arr = yyjson_mut_arr(doc_);
		yyjson_mut_doc_set_root(doc_, arr);
		return OutputArrayType(arr);
	}

	Writer::OutputObjectType Writer::object_as_root(const size_t) const noexcept {
		if (obj_) {
			return OutputObjectType(obj_);
		}

		const auto obj = yyjson_mut_obj(doc_);
		yyjson_mut_doc_set_root(doc_, obj);

		return OutputObjectType(obj);
	}

	Writer::OutputVarType Writer::null_as_root() const noexcept {
		//assert(false);
		const auto null = yyjson_mut_null(doc_);
		yyjson_mut_doc_set_root(doc_, null);
		return OutputVarType(null);
	}

	Writer::OutputArrayType Writer::add_array_to_array(
		const size_t, OutputArrayType* _parent) const noexcept {
		const auto arr = yyjson_mut_arr(doc_);
		yyjson_mut_arr_add_val(_parent->val_, arr);
		return OutputArrayType(arr);
	}

	Writer::OutputArrayType Writer::add_array_to_object(
		const std::string_view& _name, const size_t,
		OutputObjectType* _parent) const noexcept {
		const auto arr = yyjson_mut_arr(doc_);
		yyjson_mut_obj_add(_parent->val_, yyjson_mut_strcpy(doc_, _name.data()), arr);
		return OutputArrayType(arr);
	}

	Writer::OutputObjectType Writer::add_object_to_array(
		const size_t, OutputArrayType* _parent) const noexcept {
		const auto obj = yyjson_mut_obj(doc_);
		yyjson_mut_arr_add_val(_parent->val_, obj);
		return OutputObjectType(obj);
	}

	Writer::OutputObjectType Writer::add_object_to_object(
		const std::string_view& _name, const size_t,
		OutputObjectType* _parent) const noexcept {
		const auto obj = yyjson_mut_obj(doc_);
		yyjson_mut_obj_add(_parent->val_, yyjson_mut_strcpy(doc_, _name.data()), obj);
		return OutputObjectType(obj);
	}

	Writer::OutputVarType Writer::add_null_to_array(
		OutputArrayType* _parent) const noexcept {
		const auto null = yyjson_mut_null(doc_);
		yyjson_mut_arr_add_val(_parent->val_, null);
		return OutputVarType(null);
	}

	Writer::OutputVarType Writer::add_null_to_object(
		const std::string_view& _name, OutputObjectType* _parent) const noexcept {
		const auto null = yyjson_mut_null(doc_);
		yyjson_mut_obj_add(_parent->val_, yyjson_mut_strcpy(doc_, _name.data()),
						   null);
		return OutputVarType(null);
	}

	void Writer::end_array(OutputArrayType*) const noexcept {}

	void Writer::end_object(OutputObjectType*) const noexcept {}

}  // namespace rfl::json
