#pragma once

#include <thread>
#include <moodycamel/readerwriterqueue.h>
#include "Edio.h"

namespace rp {
	struct EdioSerialCommand {
		uint64_t data = 0;
		size_t size = 0;
	};

	class EdioProxy {
	private:
		moodycamel::BlockingReaderWriterQueue<EdioSerialCommand> _queue;
		std::jthread _edioThread;
		std::atomic<bool> _running{ true };
		std::atomic<bool> _initialized{ false };

	public:
		EdioProxy() {
			_edioThread = std::jthread([this]() { this->run(); });
		}

		~EdioProxy() {
			stop();
		}

		void sendCommand(const EdioSerialCommand& command) {
			if (_initialized) {
				_queue.enqueue(command);
			}
		}

		void stop() {
			if (_running) {
				spdlog::info("Stopping EdioProxy thread...");
				_running = false;
				_queue.enqueue(EdioSerialCommand{}); // Unblock the thread if it's waiting
				_edioThread.join();
			}
		}

	private:
		void run() {
			std::string port = Edio::findN8Port();
			if (port == "") {
				spdlog::error("EdioProxy failed to find Everdrive port");
				return;
			}

			Edio edio(port);
			if (!edio.isValid()) {
				spdlog::error("Failed to find Everdrive on port {}", port);
				return;
			}

			spdlog::info("Found everdrive on port {}", port);
			_initialized = true;

			EdioSerialCommand command;
			while (_running) {
				if (_queue.wait_dequeue_timed(command, std::chrono::seconds(1))) {
					if (!_running) {
						break;
					}

					if (command.size > 0) {
						edio.fifoWR(reinterpret_cast<const uint8_t*>(&command.data), 0, static_cast<int>(command.size));
					}
				}
			}

			_initialized = false;
		}
	};
}
