#include "2fish/engine/engine.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		market::Engine engine{ "77893140510362582253172593084218413010407941075415081594586195705930819989216" };
		engine.start();
	}
	catch (std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
