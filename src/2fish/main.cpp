#include "2fish/driver/driver.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		Driver driver{ "21531673013983865556883338562247005250223526242201963406740846802818002897048" };
		driver.start();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
