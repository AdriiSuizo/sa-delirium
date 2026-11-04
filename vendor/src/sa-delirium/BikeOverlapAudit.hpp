#pragma once
static void auditBikeInitialContacts(Active&a){
    // Read-only diagnostic, never a dependency of contact generation. An
    // unconditional call did 21 * every render triangle GJK queries and JSON
    // output on the admission frame (51,660 queries / 17.8 ms in case070652).
    // Keep that evidence available explicitly, off in ordinary play and perf.
    if(!GetEnvironmentVariableA("GTASA_BIKE_OVERLAP_AUDIT",nullptr,0))return;
    const double started=bikeGeometryClockMs();
    const int source=a.departingBike>=0?a.departingBike:a.vehicle;
    const auto*v=collision->findVehicle(source);
    if(!v||!v->shape||v->shape->id<0){event("bike_initial_contacts_unavailable",a.ref,a.model,"source_shape_missing");return;}
    const int type=at<int>(v->entity,0x594);if(type!=2&&type!=9&&type!=10)return;
    struct Query {const CollisionWorld::Vehicle*vehicle;std::array<float,21*8> contacts{};int overlaps=0;double elapsedMs=0;};
    std::vector<Query> queries;
    double queryMs=0;uint64_t primitives=0,derivedGjkCalls=0;
    std::array<float,21*8> nearest{};std::array<int,21> owner{};
    for(int i=0;i<21;i++){nearest[i*8]=-1;nearest[i*8+1]=1.e9f;owner[i]=-1;}
    for(const auto&part:collision->vehicles)if(part.ref==source&&part.shape&&part.shape->id>=0){
        Query q{&part};const float frame[7]={part.frame.p.x,part.frame.p.y,part.frame.p.z,part.frame.q.x,part.frame.q.y,part.frame.q.z,part.frame.q.w};
        const double queryStarted=bikeGeometryClockMs();
        q.overlaps=nm_agent_vehicle_overlap(a.agent,part.shape->id,frame,q.contacts.data(),21);
        q.elapsedMs=bikeGeometryClockMs()-queryStarted;queryMs+=q.elapsedMs;
        primitives+=part.shape->primitives.size();derivedGjkCalls+=21*part.shape->primitives.size();
        if(q.overlaps<0){event("bike_initial_contacts_unavailable",a.ref,a.model,"sdk_query_rejected component="+std::to_string(part.component));return;}
        for(int i=0;i<21;i++)if(const float*r=q.contacts.data()+i*8;r[0]>=0&&r[1]<nearest[i*8+1]){
            std::copy_n(r,8,nearest.data()+i*8);owner[i]=part.component;
        }
        queries.push_back(std::move(q));
    }
    const auto file=folder/("bike-initial-contacts-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(a.ref)+"-"+std::to_string(gameTime())+".json");
    const double writeStarted=bikeGeometryClockMs();
    std::ofstream out(file);out.precision(9);
    float deepest=0;int unanswered=0,overlaps=0;
    for(int i=0;i<21;i++){const float*r=nearest.data()+i*8;if(r[0]<0)unanswered++;else {deepest=std::min(deepest,r[1]);if(r[1]<0)overlaps++;}}
    out<<"{\"ped\":"<<a.ref<<",\"vehicle\":"<<source<<",\"model\":"<<at<short>(v->entity,0x22)<<",\"overlapping_parts\":"<<overlaps<<",\"frame\":"<<*reinterpret_cast<unsigned*>(0xB7CB4C);
    const auto*snapshot=currentBikeContactSnapshot(v->entity);
    out<<",\"representation\":\""<<(snapshot?"installed_articulated_mesh":"native_sa_col")<<"\",\"component_count\":"<<queries.size();
    out<<",\"query_ms\":"<<queryMs<<",\"primitives_queried\":"<<primitives<<",\"derived_gjk_calls\":"<<derivedGjkCalls;
    out<<",\"gjk_count_basis\":\"bike_contacts.rs_all_21_parts_times_all_registered_primitives_not_solver_counter\"";
    if(snapshot)out<<",\"native_rendered\":"<<(snapshot->renderedNative?"true":"false")<<",\"motion_verified\":"<<(snapshot->motionVerified?"true":"false")<<",\"native_local_error\":"<<snapshot->nativeMatrixError<<",\"native_world_error\":"<<snapshot->nativeWorldError;
    auto rows=[&](const auto&contacts,int component,bool aggregate){
        out<<'[';for(int i=0;i<21;i++){
            if(i)out<<',';const float*r=contacts.data()+i*8;
            out<<"{\"part\":"<<i<<",\"component\":"<<(aggregate?owner[i]:component)<<",\"primitive\":"<<int(r[0])<<",\"gap\":"<<r[1]<<",\"point\":["<<r[2]<<','<<r[3]<<','<<r[4]<<"],\"normal\":["<<r[5]<<','<<r[6]<<','<<r[7]<<"]}";
        }out<<']';
    };
    out<<",\"parts\":";rows(nearest,0,true);out<<",\"components\":[";
    for(size_t n=0;n<queries.size();n++){
        if(n)out<<',';const auto&q=queries[n];const auto&part=*q.vehicle;
        out<<"{\"component\":"<<part.component<<",\"shape\":"<<part.shape->id<<",\"primitives\":"<<part.shape->primitives.size()<<",\"query_ms\":"<<q.elapsedMs<<",\"overlapping_parts\":"<<q.overlaps;
        out<<",\"position\":["<<part.frame.p.x<<','<<part.frame.p.y<<','<<part.frame.p.z<<"],\"rotation\":["<<part.frame.q.x<<','<<part.frame.q.y<<','<<part.frame.q.z<<','<<part.frame.q.w<<"],\"parts\":";
        rows(q.contacts,part.component,false);out<<'}';
    }out<<"]}";out.close();
    const double completed=bikeGeometryClockMs();
    event("bike_initial_contacts",a.ref,a.model,"source_vehicle="+std::to_string(source)+" components="+std::to_string(queries.size())+" overlapping_parts="+std::to_string(overlaps)+" deepest_gap="+std::to_string(deepest)+" unanswered="+std::to_string(unanswered)+" file="+file.filename().string()+
        " query_ms="+std::to_string(queryMs)+" output_ms="+std::to_string(completed-writeStarted)+" total_ms="+std::to_string(completed-started)+" derived_gjk_calls="+std::to_string(derivedGjkCalls)+" output_ok="+std::to_string(!out.fail()));
}
