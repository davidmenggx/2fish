#include "2fish/engine/engine.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		market::Engine engine{};
		engine.start();
	}
	catch (std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
