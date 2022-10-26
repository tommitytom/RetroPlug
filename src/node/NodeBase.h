#pragma once

#include <memory>
#include <variant>

#include "foundation/Types.h"
#include "MessageBus.h"
#include "NodeProcessor.h"
#include "foundation/Math.h"

namespace rp {
	enum class NodeLifecycleState {
		Created,
		Initialized
	};

	using NodeIndex = int32;
	const NodeIndex INVALID_NODE_INDEX = -1;

	class NodeBase {
	private:
		NodeIndex _index = INVALID_NODE_INDEX;
		std::vector<InputBase> _inputs;
		std::vector<OutputBase> _outputs;
		NodeLifecycleState _lifecycle = NodeLifecycleState::Created;
		bool _alwaysActive = false;
		Point _position;

	protected:
		entt::registry* _shared = nullptr;

	public:
		virtual void onProcess() {}

		virtual void onInitialize() {}

		Point getPosition() const {
			return _position;
		}

		void setPosition(Point pos) {
			_position = pos;
		}

		template <typename T>
		T& getState() {
			assert(_shared);
			return _shared->ctx().at<T>();
		}

		template <typename T>
		const T& getState() const {
			assert(_shared);
			return _shared->ctx().at<const T>();
		}

		void setAlwaysActive(bool alwaysActive) {
			_alwaysActive = alwaysActive;
		}

		bool isAlwaysActive() const {
			return _alwaysActive;
		}

		void initialize() {
			onInitialize();
			_lifecycle = NodeLifecycleState::Initialized;
		}

		NodeIndex getIndex() const {
			return _index;
		}

		template <typename T>
		Output<T>* addOutput(std::string_view name, T&& value = T()) {
			_outputs.push_back(OutputBase{
				.name = std::string(name),
				.data = entt::make_any<T>(std::move(value))
			});

			assert(_outputs.back().data.owner());

			return static_cast<Output<T>*>(&_outputs.back());
		}

		template <typename T>
		Input<T>* addInput(std::string_view name, T&& value = T()) {
			_inputs.push_back(InputBase {
				.name = std::string(name),
				.defaultValue = entt::make_any<T>(std::move(value))
			});

			InputBase& input = _inputs.back();
			assert(input.defaultValue.owner());
			input.data = &input.defaultValue;

			return static_cast<Input<T>*>(&input);
		}

		InputBase* addInputCopy(std::string_view name, const entt::any& other) {
			_inputs.push_back(InputBase{
				.name = std::string(name),
				.defaultValue = other
			});

			assert(_inputs.back().defaultValue.owner());

			return &_inputs.back();
		}

		OutputBase* addOutputCopy(std::string_view name, const entt::any& other) {
			_outputs.push_back(OutputBase{
				.name = std::string(name),
				.data = other
			});

			assert(_outputs.back().data.owner());

			return &_outputs.back();
		}

		template <typename T>
		const Input<T>* getInput(size_t idx) const {
			const InputBase& input = _inputs[idx];
			assert(input.defaultValue.type() == entt::type_id<T>());
			return static_cast<const Input<T>*>(&input);
		}

		template <typename T>
		Input<T>* getInput(size_t idx) {
			InputBase& input = _inputs[idx];
			assert(input.defaultValue.type() == entt::type_id<T>());
			return static_cast<Input<T>*>(&input);
		}

		template <typename T>
		const T& getInputValue(size_t idx) const {
			return getInput<T>(idx)->value();
		}

		template <typename T>
		T& getInputValue(size_t idx) {
			return getInput<T>(idx)->value();
		}

		template <typename T>
		Output<T>* getOutput(size_t idx) {
			OutputBase& output = _outputs[idx];
			assert(output.data.type() == entt::type_id<T>());
			return static_cast<Output<T>*>(&output);
		}

		template <typename T>
		const Output<T>* getOutput(size_t idx) const {
			OutputBase& output = _outputs[idx];
			assert(output.data.type() == entt::type_id<T>());
			return static_cast<const Output<T>*>(&output);
		}

		template <typename T>
		T& getOutputValue(size_t idx) {
			return getOutput<T>(idx)->value();
		}

		template <typename T>
		const T& getOutputValue(size_t idx) const {
			return getOutput<T>(idx)->value();
		}

		template <typename T>
		void setOutputValue(size_t idx, T&& value) {
			getOutput<T>(idx)->setValue(std::forward<T>(value));
		}

		std::vector<InputBase>& getInputs(){
			return _inputs;
		}

		const std::vector<InputBase>& getInputs() const {
			return _inputs;
		}

		std::vector<OutputBase>& getOutputs() {
			return _outputs;
		}

		const std::vector<OutputBase>& getOutputs() const {
			return _outputs;
		}

		template <typename Proessor> friend class NodeGraph;
		friend class NodeGraphProcessor;
	};

	using NodePtr = std::shared_ptr<NodeBase>;
}
