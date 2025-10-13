#include "OrderCacheTestUtils.h"
#include <iostream>
#include <string>
#include <limits>

void clearInputBuffer() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

int main() {
    while (true) {
        cout << "\nOrderCache Test Runner Menu\n";
        cout << "1. Run Example 1 Tests\n";
        cout << "2. Run Example 2 Tests\n";
        cout << "3. Run Example 3 Tests\n";
        cout << "4. Run Stress Test\n";
        cout << "5. Run Performance Benchmarks\n";
        cout << "6. Exit\n";
        cout << "Enter your choice (1-6): ";

        int choice;
        if (!(cin >> choice)) {
            cout << "Invalid input. Please enter a number between 1 and 5.\n";
            clearInputBuffer();
            continue;
        }

        if (choice == 6) {
            cout << "Exiting Test Runner.\n";
            break;
        }

        switch (choice) {
        case 1:
            runExample1Tests();
            break;
        case 2:
            runExample2Tests();
            break;
        case 3:
            runExample3Tests();
            break;
        case 4: {
            int numOrders;
            cout << "Enter number of orders for stress test (1-1000000): ";
            if (!(cin >> numOrders) || numOrders < 1 || numOrders > 1000000) {
                cout << "Invalid number of orders. Please enter a number between 1 and 10000.\n";
                clearInputBuffer();
                continue;
            }
            runStressTest(numOrders);
            break;
        }
        case 5: {
            int numOrders;
            cout << "Enter number of orders for benchmark (1-1000000, or 0 for default sizes): ";
            if (!(cin >> numOrders) || numOrders < 0 || numOrders > 1000000) {
                cout << "Invalid number of orders. Please enter a number between 0 and 10000.\n";
                clearInputBuffer();
                continue;
            }
            runBenchmark(numOrders);
            break;
        }
        default:
            cout << "Invalid choice. Please enter a number between 1 and 5.\n";
            break;
        }

        clearInputBuffer(); // Clear any remaining input
    }

    cout << "All Selected Tests Completed!\n";
    return 0;
}