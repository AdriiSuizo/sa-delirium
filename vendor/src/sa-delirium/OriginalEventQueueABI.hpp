#pragma once
#include <cstdint>
#include <cstddef>
// Struct ABI for the generated SDK's opaque original-event handles. Source
// task/event semantics only; this header installs no hook and owns no SA task.
struct NmOriginalEventQueue;
struct NmOriginalEventInput {
    std::uint32_t size,kind;
    std::uint64_t receipt,source_generation,source_birth;
    std::int32_t priority,lifetime;
    std::uint32_t flags,admission;
    std::int32_t decision_response;
};
struct NmOriginalEventAdded {std::uint32_t stored,group,friends,vehicle,heard;};
struct NmOriginalEventRequest {
    std::uint32_t operation,kind;std::uint64_t receipt,generation,birth,source_generation,source_birth;
    std::int32_t age,priority;std::uint32_t flags,record_kind;
    std::uint64_t record_receipt,record_source_generation,record_source_birth;
    std::int32_t record_priority;std::uint32_t task,urgency,built;
};
struct NmOriginalEventReply {std::uint32_t answer;std::int32_t age;std::uint32_t flags;};
using NmOriginalEventCall=std::int32_t(*)(void*,const NmOriginalEventRequest*,NmOriginalEventReply*);
struct NmOriginalEventHost {std::uint32_t size,version;void*context;NmOriginalEventCall call;};
struct NmOriginalEventSnapshot {
    std::uint32_t queued,poisoned;std::uint64_t last_receipt,records[4];std::uint32_t note,slot2;
};
enum : std::int32_t { OriginalEventOK=1,OriginalEventArg=-1,OriginalEventIdentity=-2,
    OriginalEventBusy=-3,OriginalEventUnmapped=-4,OriginalEventPoisoned=-5,OriginalEventReplay=-6 };
enum : std::uint32_t { OriginalEventPreflight=0,OriginalEventReady=1,OriginalEventValid=2,
    OriginalEventConcerns=3,OriginalEventOverrides=4,OriginalEventRespond=5,OriginalEventAbort=6,
    OriginalEventRagdollRetry=7,OriginalEventGive=8,OriginalEventClear=9 };
static_assert(sizeof(NmOriginalEventInput)==56&&offsetof(NmOriginalEventInput,receipt)==8);
static_assert(sizeof(NmOriginalEventRequest)==104&&offsetof(NmOriginalEventRequest,record_receipt)==64);
static_assert(sizeof(NmOriginalEventSnapshot)==56);
#ifndef NM_API
#define SA_ORIGINAL_EVENT_API
#else
#define SA_ORIGINAL_EVENT_API NM_API
#endif
extern "C" {
SA_ORIGINAL_EVENT_API NmOriginalEventQueue*nm_original_event_create(std::uint64_t generation,std::uint64_t birth);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_destroy(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_prepare(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth,const NmOriginalEventInput*);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_add(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth,const NmOriginalEventInput*,NmOriginalEventAdded*);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_handle(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth,const std::uint32_t*roots6,const NmOriginalEventHost*);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_snapshot(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth,NmOriginalEventSnapshot*);
SA_ORIGINAL_EVENT_API std::int32_t nm_original_event_references(NmOriginalEventQueue*,std::uint64_t generation,std::uint64_t birth,std::uint64_t*receipts,std::int32_t capacity);
}
#undef SA_ORIGINAL_EVENT_API
