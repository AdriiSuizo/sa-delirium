//! Custom tuning for SA vehicles without a close IV counterpart.

#[derive(Clone, Copy)]
pub(crate) struct Profile {
    pub reference_mass: f32,
    pub gears: u8,
    pub drive_force: f32,
    pub drive_inertia: f32,
    pub top_mps: f32,
    pub brake: f32,
    pub traction: f32,
    pub slide_traction: f32,
    pub spring: f32,
    pub compression_damping: f32,
    pub rebound_damping: f32,
}

const SEDAN: Profile = Profile {
    reference_mass: 1650.0,
    gears: 5,
    drive_force: 0.22,
    drive_inertia: 1.0,
    top_mps: 46.0,
    brake: 0.85,
    traction: 1.10,
    slide_traction: 0.90,
    spring: 1.45,
    compression_damping: 0.16,
    rebound_damping: 0.23,
};

const COMPACT: Profile = Profile {
    reference_mass: 1100.0,
    drive_force: 0.18,
    top_mps: 40.0,
    ..SEDAN
};
const MUSCLE: Profile = Profile {
    reference_mass: 1750.0,
    gears: 4,
    drive_force: 0.29,
    top_mps: 53.0,
    slide_traction: 0.80,
    ..SEDAN
};
const FAST_SPORTS: Profile = Profile {
    drive_force: 0.26,
    top_mps: 61.0,
    traction: 1.24,
    slide_traction: 1.03,
    spring: 1.62,
    ..SEDAN
};
const OFF_ROAD: Profile = Profile {
    reference_mass: 2200.0,
    drive_force: 0.24,
    top_mps: 42.0,
    traction: 1.14,
    slide_traction: 0.94,
    spring: 1.25,
    rebound_damping: 0.27,
    ..SEDAN
};
const COMMERCIAL: Profile = Profile {
    reference_mass: 2900.0,
    drive_force: 0.20,
    drive_inertia: 0.8,
    top_mps: 38.0,
    brake: 0.76,
    traction: 1.04,
    slide_traction: 0.85,
    spring: 1.28,
    compression_damping: 0.21,
    rebound_damping: 0.29,
    ..SEDAN
};

const POLICE: Profile = Profile {
    drive_force: 0.26,
    brake: 0.98,
    top_mps: 50.0,
    ..SEDAN
};
const HEAVY: Profile = Profile {
    reference_mass: 5500.0,
    top_mps: 32.0,
    ..COMMERCIAL
};
const WORK: Profile = Profile {
    reference_mass: 1400.0,
    drive_force: 0.18,
    top_mps: 24.0,
    spring: 1.6,
    ..COMMERCIAL
};
const LIGHT_OFF_ROAD: Profile = Profile {
    reference_mass: 400.0,
    drive_force: 0.18,
    top_mps: 38.0,
    ..OFF_ROAD
};
const BUGGY: Profile = Profile {
    reference_mass: 850.0,
    drive_force: 0.23,
    spring: 1.20,
    rebound_damping: 0.32,
    ..OFF_ROAD
};
const HOT_ROD: Profile = Profile {
    reference_mass: 1400.0,
    spring: 1.55,
    ..MUSCLE
};
const STOCK_CAR: Profile = Profile {
    reference_mass: 1600.0,
    drive_force: 0.31,
    brake: 1.05,
    traction: 1.42,
    slide_traction: 1.14,
    spring: 1.85,
    ..FAST_SPORTS
};
const DESERT_TRUCK: Profile = Profile {
    reference_mass: 4500.0,
    drive_force: 0.25,
    spring: 1.2,
    rebound_damping: 0.34,
    ..OFF_ROAD
};
const KART: Profile = Profile {
    reference_mass: 300.0,
    drive_force: 0.17,
    top_mps: 24.0,
    spring: 1.8,
    ..COMPACT
};
const RC: Profile = Profile {
    reference_mass: 100.0,
    drive_force: 0.14,
    top_mps: 18.0,
    ..KART
};
const MONSTER: Profile = Profile {
    reference_mass: 5000.0,
    drive_force: 0.23,
    spring: 1.55,
    ..OFF_ROAD
};

pub(crate) fn for_model(model: u32, mass: f32, length: f32, height: f32) -> Profile {
    match model {
        406 => HEAVY,
        424 | 568 => BUGGY,
        434 | 545 => HOT_ROD,
        494 | 502 | 503 => STOCK_CAR,
        573 => DESERT_TRUCK,
        445 | 426 => SEDAN,              // Admiral, Premier
        410 | 496 | 436 => COMPACT,      // Manana, Blista Compact, Previon
        402 | 475 | 603 | 518 => MUSCLE, // Buffalo, Sabre, Phoenix, Buccaneer
        411 | 415 | 429 | 451 | 477 | 480 | 506 | 541 | 558 | 559 | 560 | 562 | 587 |
        602 => FAST_SPORTS,
        400 | 489 | 500 | 490 | 599 => OFF_ROAD,
        444 | 556 | 557 => MONSTER,
        471 => LIGHT_OFF_ROAD,
        441 | 594 => RC,
        571 => KART,
        457 | 485 | 525 | 530 | 531 | 532 | 572 | 574 | 583 => WORK,
        482 | 440 | 498 | 427 => COMMERCIAL,
        596..=598 | 528 => POLICE,
        _ if mass >= 4200.0 => HEAVY,
        _ if mass >= 2400.0 || height >= 2.3 => COMMERCIAL,
        _ if length < 4.0 && mass < 1400.0 => COMPACT,
        _ => SEDAN,
    }
}
