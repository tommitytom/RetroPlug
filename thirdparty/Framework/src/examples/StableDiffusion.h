#pragma once

#include <future>
#include <thread>

#include <entt/core/type_info.hpp>

#include <json.hpp>
#include <simdjson/simdjson.h>
#include <httplib.h>

using json = nlohmann::json;

#include "foundation/Attributes.h"
#include "foundation/node/NodeState.h"
#include "foundation/node/NodeGraphCompiler.h"
#include "foundation/node/NodeFactory.h"
#include "foundation/node/NodeGraph.h"
#include "foundation/node/Node.h"
#include "foundation/node/NodeRegistry.h"
#include "foundation/FsUtil.h"

#include "ui/View.h"
#include "ui/ButtonView.h"
#include "ui/TextureView.h"

#include "application/Application.h"

#define ASIO_STANDALONE
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include <moodycamel/readerwriterqueue.h>

typedef websocketpp::client<websocketpp::config::asio_client> client;

using websocketpp::lib::placeholders::_1;
using websocketpp::lib::placeholders::_2;
using websocketpp::lib::bind;

typedef websocketpp::config::asio_client::message_type::ptr message_ptr;

namespace fw {
	struct ComfyProgressEvent {
		uint32 value = 0;
		uint32 max = 0;
		f32 percentage = 0.0f;
	};

	struct ComfyExecutingEvent {
		uint32 node = 0;
	};

	struct ComfyStatusEvent {
		uint32 queueRemaining = 0;
		std::optional<std::string> id;
	};

	struct ComfyExecutedEvent {
		struct Output {
			struct Image {
				std::string filename;
				std::string subfolder;
				std::string type;
			};
			
			std::vector<Image> images;
		};

		uint32 node = 0;
		Output output;
	};

	class ComfyClient {
	private:
		std::jthread _thread;
		client _client;
		httplib::Client _http;
		std::string _url;
		moodycamel::ReaderWriterQueue<std::string> _messages;
		std::unordered_map<entt::id_type, std::function<void(entt::any&&)>> _eventHandlers;

		std::string _id;

	public:
		ComfyClient(const std::string& url): _url(url), _http("http://" + url) {
			try {
				// Set logging to be pretty verbose (everything except message payloads)
				_client.set_access_channels(websocketpp::log::alevel::all);
				_client.clear_access_channels(websocketpp::log::alevel::frame_payload);

				// Initialize ASIO
				_client.init_asio();

				// Register our message handler
				_client.set_message_handler(bind(&ComfyClient::on_message, &_messages, ::_1, ::_2));

				websocketpp::lib::error_code ec;
				client::connection_ptr con = _client.get_connection("ws://" + url + "/ws", ec);
				if (ec) {
					std::cout << "could not create connection because: " << ec.message() << std::endl;
					return;
				}

				// Note that connect here only requests a connection. No network messages are
				// exchanged until the event loop starts running in the next line.
				_client.connect(con);

				// Start the ASIO io_service run loop
				// this will cause a single connection to be made to the server. _client.run()
				// will exit when this connection is closed.
				_thread = std::jthread([this, url]() {
					_http.Get("/extensions");
					_http.Get("/embeddings");
					_http.Get("/object_info");
					
					_client.run();
				});
			} catch (websocketpp::exception const& e) {
				std::cout << e.what() << std::endl;
			}
		}

		~ComfyClient() {
			_thread.request_stop();
		}

		const std::string& getId() const {
			return _id;
		}

		std::string getNodes() {
			auto res = _http.Get("/object_info");
			return res->body;
		}

		void prompt(std::string data) {
			_http.Post("/prompt", data, "application/json");
		}

		template <typename T>
		void on(std::function<void(const T&)>&& func) {
			_eventHandlers[entt::type_id<T>().index()] = [func = std::move(func)](entt::any&& v) {
				func(entt::any_cast<T>(v));
			};
		}

		template <typename T>
		void emit(T&& event) {
			auto found = _eventHandlers.find(entt::type_id<T>().index());
			if (found != _eventHandlers.end()) {
				found->second(std::forward<T>(event));
			}
		}

		void update() {
			std::string msg;
			while (_messages.try_dequeue(msg)) {
				spdlog::debug(msg);

				simdjson::padded_string padded(msg);
				simdjson::ondemand::parser parser;
				simdjson::ondemand::document doc = parser.iterate(padded);
				
				std::string_view type = doc["type"].get_string();
				auto data = doc["data"];
				
				if (type == "status") {
					auto status = data["status"];
					auto execInfo = status["exec_info"];
					ComfyStatusEvent ev{ (uint32)execInfo["queue_remaining"].get_uint64() };

					auto sid = data["sid"].get_string();
					if (!sid.error()) {
						_id = sid.value();
						spdlog::info("Assigned ID: {}", _id);
						ev.id = _id;
					}

					emit(std::move(ev));
				} else if (type == "progress") {
					auto value = data["value"];
					auto max = data["max"];

					emit(ComfyProgressEvent{ (uint32)value.get_uint64().value(), (uint32)max.get_uint64().value() });
				} else if (type == "executing") {
					auto node = data["node"];
					uint32 nodeId = 0;
					
					if (!node.is_null()) {
						nodeId = (uint32)node.get_uint64_in_string();
					}

					emit(ComfyExecutingEvent{ nodeId });
				} else if (type == "executed") {
					ComfyExecutedEvent ev;

					ev.node = (uint32)data["node"].get_uint64_in_string();
					auto output = data["output"];
					auto images = output["images"];

					for (auto image : images) {
						auto filename = image["filename"];
						auto subfolder = image["subfolder"];
						auto type = image["type"];

						ev.output.images.push_back(ComfyExecutedEvent::Output::Image{
							.filename = std::string(filename.get_string().value()),
							.subfolder = std::string(subfolder.get_string().value()),
							.type = std::string(type.get_string().value())
						});
					}

					emit(std::move(ev));
				}
			}
		}

	private:
		static void on_message(moodycamel::ReaderWriterQueue<std::string>* q, websocketpp::connection_hdl hdl, message_ptr msg) {
			q->emplace(msg->get_payload());
		}
	};
	
	class ComfyGraph {
	private:
		void process() {
			
		}
	};

	const fs::path COMFY_OUTPUT_PATH = "E:\\code\\ComfyUI\\output\\";

	class StableDiffusion : public View {
	private:
		NodeRegistry _nodeRegistry;
		std::unique_ptr<ComfyClient> _client;
		std::shared_ptr<TextureView> _output;

	public:
		StableDiffusion() : View({ 1024, 768 }) {
			setType<StableDiffusion>();
			setFocusPolicy(FocusPolicy::Click);

			_client = std::make_unique<ComfyClient>("127.0.0.1:8188");

			_client->on<ComfyProgressEvent>([](const ComfyProgressEvent& ev) {
				spdlog::info("Progress: {}/{}", ev.value, ev.max);
			});

			_client->on<ComfyExecutedEvent>([&](const ComfyExecutedEvent& ev) {
				fs::path imagePath = COMFY_OUTPUT_PATH / ev.output.images[0].filename;
				spdlog::info(imagePath.string());
				TextureHandle tex = getResourceManager().load<Texture>(imagePath.string());
				_output->setTexture(tex);
			});
		}

		~StableDiffusion() = default;

		void onInitialize() override {
			extractNodes();
			
			getLayout().setDimensions(100_pc);

			auto button = addChild<ButtonView>("Button");
			button->getLayout().setPositionEdge(FlexEdge::Left, 10);
			button->getLayout().setPositionEdge(FlexEdge::Top, 10);

			_output = addChild<TextureView>("Output");
			_output->setArea(Rect(10, 100, 400, 400));
			
			subscribe<ButtonClickEvent>(button, [this]() {
				std::string prompt = FsUtil::readTextFile("C:\\temp\\graph.json");
				json data = json::parse(prompt);
				data["client_id"] = _client->getId();
				_client->prompt(data.dump());
			});
		}

		void extractNodes() {
			std::string body = _client->getNodes();
			simdjson::padded_string padded(body);
			simdjson::ondemand::parser parser;
			simdjson::ondemand::document doc = parser.iterate(padded);
			simdjson::ondemand::object nodeMap = doc.get_object();

			for (auto nodeDesc : nodeMap) {
				auto factory = _nodeRegistry.addNode(nodeDesc.unescaped_key());
				factory.setCategory("Stable Diffusion");

				simdjson::ondemand::object inputFields = nodeDesc.value()["input"]["required"].get_object();

				for (auto field : inputFields) {
					std::string fieldName = std::string(field.unescaped_key().value());
					simdjson::ondemand::array fieldItems = field.value().get_array();
					std::string_view typeName;
					entt::id_type typeHash;
					NodeRegistry::Node::Input* input = nullptr;

					size_t idx = 0;
					for (auto fieldItem : fieldItems) {
						if (idx == 0) {
							std::vector<std::string> options;
							auto typeNameField = fieldItem.get_string();

							if (typeNameField.error() == simdjson::error_code::SUCCESS) {
								typeName = typeNameField.value();
							} else {
								typeName = "STRING";

								auto strArr = fieldItem.get_array();

								for (auto str : strArr) {
									options.push_back(std::string(str.get_string().value()));
								}
							}

							typeHash = entt::hashed_string(typeName.data()).value();
							input = &factory.addInput(fieldName, typeHash);

							if (options.size()) {
								input->attribs.push_back(fw::OptionsAttribute<std::string>(options));
							}
						} else if (idx == 1) {
							auto attribData = fieldItem.get_object();

							if (attribData.error() == simdjson::error_code::SUCCESS) {
								if (typeName == "INT" || typeName == "FLOAT") {
									auto def = attribData["default"];
									auto min = attribData["min"];
									auto max = attribData["max"];
									auto step = attribData["step"];

									if (min.is_scalar() && max.is_scalar()) {
										input->attribs.push_back(fw::RangeAttribute{
											(f32)min.value().get_double(),
											(f32)max.value().get_double()
										});
									}
								} else if (typeName == "STRING") {
									auto def = attribData["default"].get_string();

									if (def.error() == simdjson::error_code::SUCCESS) {
										input->attribs.push_back(fw::DefaultAttribute(std::string(def.value())));
									}
								}
							}
						}

						idx++;
					}
				}

				std::vector<entt::id_type> outputTypes;
				simdjson::ondemand::array outputFields = nodeDesc.value()["output"];

				for (auto outputField : outputFields) {
					outputTypes.push_back(entt::hashed_string::value(outputField.get_string().value().data()));
				}

				size_t idx = 0;
				simdjson::ondemand::array outputNames = nodeDesc.value()["output_name"];

				for (auto outputName : outputNames) {
					factory.addOutput(outputName.get_string(), outputTypes[idx++]);
				}

				factory.setDisplayName(nodeDesc.value()["display_name"].value());
				factory.setDescription(nodeDesc.value()["description"].value());
				factory.setSubCategory(nodeDesc.value()["category"].value());
			}
		}

		bool onMouseButton(const MouseButtonEvent& ev) override {
			return false;
		}

		bool onKey(const KeyEvent& ev) override {
			return false;
		}

		void onUpdate(f32 delta) override {
			_client->update();
		}

		void onRender(fw::Canvas& canvas) override {

		}
	};

	using StableDiffusionApplication = fw::app::BasicApplication<StableDiffusion, void>;
}
