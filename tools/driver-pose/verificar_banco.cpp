#define DRIVER_POSE_TEST
#include "DriverPose.cpp"
#include <iostream>

int wmain(int argc, wchar_t** argv) {
    if (argc != 2 || !readBanks(argv[1])) return 1;
    unsigned seats = 0;
    for (const auto& group : groups) {
        for (const auto& seat : group) {
            if (!seat.aim.empty()) ++seats;
        }
    }
    std::cout << seats << " seat profiles, " << 4 * seats << " valid IV clips (DBV4)\n";
    return seats == 24 ? 0 : 2;
}
