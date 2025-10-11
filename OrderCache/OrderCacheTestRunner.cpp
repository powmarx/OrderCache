#include "OrderCacheTestUtils.h"
#include <iostream>
#include <string>
#include <limits>

void clearInputBuffer() {
    std::cin.clear();
    std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
}

int main() {
    while (true) {
        std::cout << "\nOrderCache Test Runner Menu\n";
        std::cout << "1. Run Example 1 Tests\n";
        std::cout << "2. Run Example 2 Tests\n";
        std::cout << "3. Run Stress Test\n";
        std::cout << "4. Run Performance Benchmarks\n";
        std::cout << "5. Exit\n";
        std::cout << "Enter your choice (1-5): ";

        int choice;
        if (!(std::cin >> choice)) {
            std::cout << "Invalid input. Please enter a number between 1 and 5.\n";
            clearInputBuffer();
            continue;
        }

        if (choice == 5) {
            std::cout << "Exiting Test Runner.\n";
            break;
        }

        switch (choice) {
        case 1:
            runExample1Tests();
            break;
        case 2:
            runExample2Tests();
            break;
        case 3: {
            int numOrders;
            std::cout << "Enter number of orders for stress test (1-1000000): ";
            if (!(std::cin >> numOrders) || numOrders < 1 || numOrders > 1000000) {
                std::cout << "Invalid number of orders. Please enter a number between 1 and 10000.\n";
                clearInputBuffer();
                continue;
            }
            runStressTest(numOrders);
            break;
        }
        case 4: {
            int numOrders;
            std::cout << "Enter number of orders for benchmark (1-1000000, or 0 for default sizes): ";
            if (!(std::cin >> numOrders) || numOrders < 0 || numOrders > 1000000) {
                std::cout << "Invalid number of orders. Please enter a number between 0 and 10000.\n";
                clearInputBuffer();
                continue;
            }
            runBenchmark(numOrders);
            break;
        }
        default:
            std::cout << "Invalid choice. Please enter a number between 1 and 5.\n";
            break;
        }

        clearInputBuffer(); // Clear any remaining input
    }

    std::cout << "All Selected Tests Completed!\n";
    return 0;
}