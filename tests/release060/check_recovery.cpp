#include "CjRecoveryDamage.hpp"
#include "CjRecoveryTiming.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

static int checks;
static void expect(bool ok, const char* description) {
    ++checks;
    if (!ok) throw std::runtime_error(description);
}

static void checkDamage() {
    for (int w = 0; w <= 58; ++w) {
        const bool bullet = (w >= 22 && w <= 34) || w == 38 || w == 52;
        expect(cj_recovery::preserveOnDamage(w, true, false, false, 99.f, true) == bullet,
               "weapon class");
        if (!bullet) continue;
        expect(cj_recovery::preserveOnDamage(w, true, false, false, 1.f, true), "last surviving HP");
        expect(!cj_recovery::preserveOnDamage(w, true, false, false, .5f, true), "native below-one boundary");
        expect(!cj_recovery::preserveOnDamage(w, true, false, false, 0.f, true), "dead actor");
        expect(!cj_recovery::preserveOnDamage(w, true, true, false, 99.f, true), "health-zero flag");
        expect(!cj_recovery::preserveOnDamage(w, true, false, true, 99.f, true), "forced death");
        expect(!cj_recovery::preserveOnDamage(w, false, false, false, 99.f, true), "uncalculated event");
        expect(!cj_recovery::preserveOnDamage(w, true, false, false, 99.f, false), "unowned task");
    }
}

static void checkClips(float seconds, const char* name) {
    expect(seconds > .5f && seconds < 10.f, "measured get-up clip duration");
    for (const int fps : {30, 60, 120}) {
        int finished[2]{};
        for (int cj = 0; cj <= 1; ++cj) {
            const float blend = cj_recovery::blendSeconds(cj != 0, .60f);
            const float rate = cj_recovery::clipRate(cj != 0);
            const int blendFrames = static_cast<int>(std::ceil(fps * blend));
            float position = 0.f;
            for (int tick = 1; tick <= fps * 12; ++tick) {
                if (tick > blendFrames) position += rate / fps;
                if (position >= seconds) { finished[cj] = tick; break; }
            }
            expect(finished[cj] > blendFrames, "native clip reaches its end after blending");
            expect(std::fabs(finished[cj] / static_cast<float>(fps) - (blend + seconds / rate)) < 2.1f / fps,
                   "play complete authored duration at the chosen rate");
            if (!cj) expect(rate == 1.f && blend == .60f, "NPC timing");
        }
        expect(finished[1] < finished[0], "CJ completes earlier");
        if (fps == 60) std::printf("%s authored=%.3f cj_blend_and_clip=%.3f npc_blend_and_clip=%.3f\n",
                                    name, seconds, finished[1] / 60.f, finished[0] / 60.f);
    }
}

int main(int argc, char** argv) try {
    if (argc != 3) return 2;
    checkDamage();
    expect(cj_recovery::responseSeconds(true, 0.f) == 0.f, "finished vehicle task");
    expect(cj_recovery::responseSeconds(true, .1f) == .1f, "shorter response config");
    expect(cj_recovery::stableSeconds(true, .15f) == .15f, "shorter stability config");
    expect(cj_recovery::blendSeconds(true, .2f) == .2f, "shorter blend config");
    expect(cj_recovery::responseSeconds(false, 3.f) == 3.f &&
           cj_recovery::stableSeconds(false, .35f) == .35f, "NPC response remains unchanged");
    checkClips(static_cast<float>(std::atof(argv[1])), "GETUP");
    checkClips(static_cast<float>(std::atof(argv[2])), "GETUP_FRONT");
    std::printf("%d checks passed\n", checks);
    return 0;
} catch (const std::exception& e) {
    std::fprintf(stderr, "FAIL: %s\n", e.what());
    return 1;
}
