#include "ConcurrentQaPadClock.hpp"
#include <cassert>
#include <cstring>

int main(){
    using qa_pad::DurationField;
    std::uint32_t value=0;
    assert(qa_pad::durationField("{\"ms\":8000}",value)==DurationField::Missing);
    for(auto valid:{1u,850u,1150u,5000u}){
        assert(qa_pad::durationField("{\"game_ms\": "+std::to_string(valid)+",\"ms\":8000}",value)==DurationField::Valid);
        assert(value==valid);
    }
    for(auto invalid:{"0","5001","-1","1.5","850e0","null","\"850\"","true","01","NaN","999999999999999999999"})
        assert(qa_pad::durationField(std::string("{\"game_ms\":")+invalid+"}",value)==DurationField::Invalid);
    assert(qa_pad::durationField("{\"game_ms\":850,\"game_ms\":850}",value)==DurationField::Invalid);

    for(auto duration:{850u,1150u}){
        qa_pad::Clock clock;clock.arm(duration);
        assert(clock.pending&&!clock.active&&clock.start==0);
        // The command acknowledgment/timer does not define the input start.
        assert(clock.poll(true,10000,100,1000,9000));
        assert(clock.active&&!clock.pending&&clock.start==10000&&clock.startFrame==100);
        assert(clock.deadline==10000+duration&&clock.requested==duration);
        assert(clock.poll(true,10000+duration-1,101,2000,9000));
        assert(!clock.poll(true,10000+duration,102,2001,9000));
        assert(!clock.active&&clock.stopped-clock.start==duration&&clock.stopFrame==102);
        assert(std::strcmp(clock.reason,"simulation_deadline")==0);
        assert(!clock.poll(true,10000+duration+11,103,2012,9000));
        assert(clock.stopped==10000+duration); // Evidence persists after expiry.
    }
    qa_pad::Clock clock;clock.arm(850);
    assert(clock.poll(true,100,1,100,8100));
    assert(!clock.poll(true,958,79,1100,8100)); // First available input frame after target.
    assert(clock.stopped-clock.start==858&&std::strcmp(clock.reason,"simulation_deadline")==0);
    clock.arm(5000);assert(clock.poll(true,1000,10,1000,1100));
    assert(!clock.poll(true,1001,11,1100,1100));
    assert(std::strcmp(clock.reason,"wall_ttl")==0); // Paused/slow clock cannot extend hard TTL.
    clock.arm(850);assert(!clock.poll(true,1000,10,1000,1000));
    assert(clock.start==0&&!clock.active&&std::strcmp(clock.reason,"wall_ttl")==0);
    clock.arm(850);assert(clock.poll(true,1000,10,1000,9000));
    assert(!clock.poll(false,0,11,1001,9000));
    assert(std::strcmp(clock.reason,"game_not_ready")==0&&clock.stopped==1000);
    clock.arm(850);assert(clock.poll(true,1000,10,1000,9000));
    assert(!clock.poll(true,999,11,1001,9000));assert(std::strcmp(clock.reason,"clock_reset")==0);
    clock.arm(850);assert(clock.poll(true,0xffffff00u,10,1000,9000));
    assert(!clock.poll(true,0xffffff00u+850u,11,1001,9000));
    assert(std::strcmp(clock.reason,"simulation_deadline")==0); // Native unsigned clock wrap.
    clock.arm(850);assert(clock.poll(true,1000,10,1000,9000));
    clock.legacy(1020,12);assert(!clock.tracked&&std::strcmp(clock.reason,"replaced")==0);
    assert(clock.poll(false,0,13,1100,1200)); // Commands without game_ms retain wall-time semantics.
    assert(!clock.poll(true,0,14,1200,1200));
    clock.arm(850);clock.reject(1200,15,"invalid_game_ms");
    assert(!clock.poll(true,1300,16,1201,9000));
    assert(std::strcmp(clock.reason,"invalid_game_ms")==0);
}
