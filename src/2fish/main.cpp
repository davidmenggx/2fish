#include "2fish/driver/driver.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		Driver driver{ "92040316102010961100998784965059809652087034058558799904047144229298154124933" };
		driver.start();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
