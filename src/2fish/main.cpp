#include "2fish/driver/driver.h"

#include <exception>
#include <format>
#include <iostream>

int main() {
	try {
		Driver driver{ "112199164406805486549439306159838835258340225041002461170314819242438095325546" };
		driver.start();
	}
	catch (const std::exception& e) {
		std::cerr << std::format("Unexpected error occured, aborting: {}" , e.what());
	}
}
