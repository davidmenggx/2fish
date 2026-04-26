#include "2fish/driver/driver.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		Driver driver{ "86330672324333964147096321306169393106650468157315343468606098154079947112578" };
		driver.start();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
