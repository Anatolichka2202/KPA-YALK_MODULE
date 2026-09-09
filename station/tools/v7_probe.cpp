#include "orbita_stand/v7_visa_voltmeter.h"

#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <thread>

int main()
{
    try {
        orbita::stand::V7VisaVoltmeter voltmeter;
        std::cout << "V7-78/1 VISA resource: " << voltmeter.resourceName() << '\n';
        for (int index = 1; index <= 3; ++index) {
            const double value = voltmeter.readVoltage();
            std::cout << "Reading " << index << ": "
                      << std::fixed << std::setprecision(9) << value << " V\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "V7-78/1 probe failed: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
