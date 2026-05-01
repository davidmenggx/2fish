#include "2fish/driver/driver.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		Driver driver{ "91189130255938900837454255555592253842978765492737167281377547862039475195989" };
		driver.start();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
