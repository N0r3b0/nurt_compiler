#include <iostream>
#include <vector>
#include <ranges>
#include <concepts>

// Definicja konceptu z C++20 - funkcja przyjmuje tylko liczby całkowite
template<typename T>
concept Integral = std::is_integral_v<T>;

template<Integral T>
void print_even_numbers(const std::vector<T>& numbers) {
    // Użycie widoków (Views) z biblioteki <ranges> (C++20)
    auto even_numbers = numbers 
                      | std::views::filter([](int n) { return n % 2 == 0; });

    std::cout << "Liczby parzyste: ";
    for (int n : even_numbers) {
        std::cout << n << " ";
    }
    std::cout << std::endl;
}

int main() {
    std::cout << "Test C++20 uruchomiony pomyślnie!" << std::endl;
    std::vector<int> dane = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    print_even_numbers(dane);
    return 0;
}