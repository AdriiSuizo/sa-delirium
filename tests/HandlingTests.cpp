#include "../src/Handling.hpp"
#include <cassert>
#include <string>

static std::string row(std::string id, const char* damping) {
    for (int field = 1; field <= 35; ++field) {
        id += '\t';
        id += field == 22 ? damping : "1";
    }
    return id;
}

int main() {
    const auto cars = "cars\r\n10, car, texture, car, ROAD, car\r\n"
        "11, bike, texture, bike, TWO, bike\r\n"
        "12, both, texture, car, SHARED, car\r\n"
        "13, boat, texture, boat, SHARED, boat\r\nend\r\n";
    const std::string input = "; original\r\n" + row("ROAD", "0.1") + " ; keep\r\n"
        + row("TWO", "0.2") + "\r\n" + row("SHARED", "0.2") + "\r\n";
    const auto output = tuneSuspension(input, cars, 8);
    assert(output.adjusted == 1);
    assert(output.text == "; original\r\n" + row("ROAD", "0.108000") + " ; keep\r\n"
        + row("TWO", "0.2") + "\r\n" + row("SHARED", "0.2") + "\r\n");
    assert(tuneSuspension(input, cars, 0).text == input);
    assert(tuneSuspension(row("ROAD", "bogus"), cars, 8).adjusted == 0);
    assert(tuneSuspension(row("ROAD", "0.24"), cars, 99).text == row("ROAD", "0.250000"));
    assert(tuneSuspension("ROAD 0.1\n", cars, 8).adjusted == 0);
}
