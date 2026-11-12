//! First-car experiment. SA provides wheel contacts and integrates the chassis.
//! The wheel and transmission algorithms use the existing IV-backed data crate.
//! The wheel callbacks and powertrain flow follow Delirium's Playroom car.
use crate::{vehicle_counterparts, vehicle_profiles::{self, Profile}};
use delirium_data::{
    suspension::Spring,
    transmission::{Controls, Transmission},
    tyre::{self, Contact, Tyre},
    vehicles::{self, Handling},
    wheelstep::{self, Body, Mass, Out, Strut, Times, Tread, V4, Wheel},
};
use std::{collections::HashMap, sync::OnceLock};
use std::sync::atomic::{AtomicBool, Ordering};

const AXLE: V4 = [1.0, 0.0, 0.0, 0.0];
static ADMIRAL: OnceLock<SuspensionProfile> = OnceLock::new();
static IV_HANDLING: OnceLock<HashMap<String, Handling>> = OnceLock::new();
static QUICKER_DRIVING: AtomicBool = AtomicBool::new(true);
const RESEED_WHEELS: u32 = 1;
const RESET_DRIVER: u32 = 2;
const VEHICLE_PACE: f32 = 1.3;
const MAX_SA_FRAME_SECONDS: f32 = 3.0 * 0.02;

#[unsafe(no_mangle)]
pub extern "C" fn sa_vehicle_lab_set_quicker_driving(enabled: i32) {
    QUICKER_DRIVING.store(enabled != 0, Ordering::Relaxed);
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_delirium_set_gravity(enabled: i32) {
    delirium::ragdoll::set_gravity_enabled(enabled != 0);
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_delirium_set_moon_gravity(enabled: i32) {
    delirium::ragdoll::set_moon_gravity(enabled != 0);
}

fn verified_handling(h: &Handling) -> bool {
    [
        (h.drive_force, 0.01, 1.0),
        (h.drive_inertia, 0.05, 4.0),
        (h.brake_force, 0.05, 3.0),
        (h.handbrake, 0.0, 4.0),
        (h.steering_lock, 0.1, 1.2),
        (h.traction_max, 0.3, 4.0),
        (h.traction_min, 0.1, h.traction_max),
        (h.traction_lateral, 0.05, 1.0),
        (h.traction_spring_delta_max, 0.0, 2.0),
        (h.suspension_force, 0.1, 10.0),
        (h.suspension_comp_damp, 0.0, 3.0),
        (h.suspension_rebound_damp, 0.0, 3.0),
        (h.suspension_upper - h.suspension_lower, 0.05, 1.5),
    ].iter().all(|&(value, min, max)| (min..=max).contains(&value))
        && [h.drive_bias, h.brake_bias, h.traction_bias, h.suspension_bias]
            .iter().flatten().all(|v| (0.0..=2.0).contains(v))
}

fn iv_tuning(model: u32) -> Option<&'static Handling> {
    IV_HANDLING.get()?.get(vehicle_counterparts::counterpart(model)?)
}

#[cfg(windows)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_load_handling(path: *const u16, len: usize) -> usize {
    use std::os::windows::ffi::OsStringExt;
    if path.is_null() || len == 0 || !delirium_data::kv::open() {
        return 0;
    }
    if let Some(table) = IV_HANDLING.get() {
        return table.len();
    }
    let path = std::ffi::OsString::from_wide(unsafe { std::slice::from_raw_parts(path, len) });
    let Ok(bytes) = std::fs::read(path) else { return 0 };
    let mut table = vehicles::read_handling(&String::from_utf8_lossy(&bytes));
    table.retain(|_, handling| verified_handling(handling));
    let _ = IV_HANDLING.set(table);
    IV_HANDLING.get().map_or(0, HashMap::len)
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_vehicle_lab_tuning_source(model: u32) -> i32 {
    if iv_tuning(model).is_some() {
        1
    } else if vehicle_counterparts::SPECIAL_MODELS.contains(&model) {
        2
    } else {
        0
    }
}

fn scaled_inertia(model: u32, cuboid: V4) -> V4 {
    let Some(profile) = ADMIRAL.get().filter(|_| model == 445) else {
        return cuboid;
    };
    std::array::from_fn(|axis| {
        cuboid[axis] * profile.inertia_ratio.get(axis).copied().unwrap_or(1.0)
    })
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_inertia(
    model: u32,
    cuboid: *const V4,
    output: *mut V4,
) -> i32 {
    let (Some(cuboid), Some(output)) = (unsafe { cuboid.as_ref() }, unsafe { output.as_mut() })
    else {
        return 0;
    };
    if !cuboid[..3].iter().all(|v| v.is_finite() && *v > 0.0) {
        return 0;
    }
    *output = scaled_inertia(model, *cuboid);
    1
}

#[derive(Clone, Copy)]
struct SuspensionProfile {
    inertia_ratio: [f32; 3],
    spring: f32,
    compression_damping: f32,
    rebound_damping: f32,
    bias: [f32; 2],
    stroke: f32,
    angular_damping: [f32; 3],
}

impl SuspensionProfile {
    fn from_handling(h: &Handling) -> Self {
        Self {
            inertia_ratio: [1.0; 3],
            spring: h.suspension_force,
            compression_damping: h.suspension_comp_damp,
            rebound_damping: h.suspension_rebound_damp,
            bias: h.suspension_bias,
            stroke: h.suspension_upper - h.suspension_lower,
            angular_damping: [0.0; 3],
        }
    }

    fn read(bytes: &[u8]) -> Option<Self> {
        let data: serde_json::Value = serde_json::from_slice(bytes).ok()?;
        if data["model"].as_str()? != "admiral" {
            return None;
        }
        let scalar = |key: &str| -> Option<f32> { serde_json::from_value(data[key].clone()).ok() };
        let [width, length, height]: [f32; 3] =
            serde_json::from_value(data["collision_dimensions_m"].clone()).ok()?;
        let inertia: [f32; 3] = serde_json::from_value(data["inertia_per_kg"].clone()).ok()?;
        let damping: [f32; 6] = serde_json::from_value(data["fragment_damping"].clone()).ok()?;
        let bias: [f32; 2] = serde_json::from_value(data["spring_axle_bias"].clone()).ok()?;
        let upper = scalar("upper_stop_m")?;
        let lower = scalar("lower_stop_m")?;
        let stroke = upper - lower;
        let spring = scalar("spring_force")?;
        let compression_damping = scalar("compression_damping")?;
        let rebound_damping = scalar("rebound_damping")?;
        if ![width, length, height]
            .into_iter()
            .all(|v| (0.25..=15.0).contains(&v))
            || !inertia.into_iter().all(|v| (0.01..=40.0).contains(&v))
            || !bias.into_iter().all(|v| (0.0..=2.0).contains(&v))
            || !(0.05..=1.0).contains(&stroke)
            || upper <= 0.0
            || lower >= 0.0
            || !(0.1..=10.0).contains(&spring)
            || ![compression_damping, rebound_damping, damping[3], damping[5]]
                .into_iter()
                .all(|v| (0.0..=2.0).contains(&v))
        {
            return None;
        }
        let sums = [
            length * length + height * height,
            width * width + height * height,
            width * width + length * length,
        ];
        Some(Self {
            inertia_ratio: std::array::from_fn(|axis| 12.0 * inertia[axis] / sums[axis]),
            spring,
            compression_damping,
            rebound_damping,
            bias,
            stroke,
            // CVehicleModelInfo sets the angular V term for the hinged
            // Admiral (TU8 0x8250BCE8). The fragment supplies C and V squared.
            angular_damping: [damping[3], 0.05, damping[5]],
        })
    }

    fn torque(self, spin: V4, basis: &[V4; 4], inertia: V4, dt: f32) -> V4 {
        let dot = |a: V4, b: V4| a[0].mul_add(b[0], a[1].mul_add(b[1], a[2] * b[2]));
        let local: [f32; 3] = std::array::from_fn(|axis| dot(basis[axis], spin));
        let speed = dot(spin, spin).sqrt();
        let [constant, linear, quadratic] = self.angular_damping;
        let torque: [f32; 3] = std::array::from_fn(|axis| {
            let omega = local[axis];
            let damp = constant + (linear + quadratic * speed) * omega.abs();
            -damp.min(omega.abs() / dt).copysign(omega) * inertia[axis]
        });
        std::array::from_fn(|i| {
            basis[0][i] * torque[0] + basis[1][i] * torque[1] + basis[2][i] * torque[2]
        })
    }
}

#[cfg(windows)]
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_load_admiral(path: *const u16, len: usize) -> i32 {
    use std::os::windows::ffi::OsStringExt;
    if path.is_null() || len == 0 {
        return 0;
    }
    let path = std::ffi::OsString::from_wide(unsafe { std::slice::from_raw_parts(path, len) });
    let Some(profile) = std::fs::read(path)
        .ok()
        .and_then(|bytes| SuspensionProfile::read(&bytes))
    else {
        return 0;
    };
    i32::from(ADMIRAL.set(profile).is_ok())
}

#[repr(C)]
#[derive(Clone, Copy)]
struct Ground {
    point: V4,
    normal: V4,
    travel: f32,
    compression: f32,
    radius: f32,
    load_share: f32,
    supported: u32,
}

#[repr(C)]
#[derive(Clone, Copy)]
pub struct Frame {
    basis: [V4; 4],
    velocity: V4,
    spin: V4,
    native_spin_loss: V4,
    inertia: V4,
    contacts: [Ground; 4],
    mass: f32,
    dt: f32,
    gas: f32,
    brake: f32,
    steer: f32,
    now_ms: u32,
    reseed_contacts: u32,
    handbrake: u32,
    nitro: u32,
    gravity_scale: f32,
}

#[repr(C)]
pub struct Result {
    velocity: V4,
    spin: V4,
    spring_force: [f32; 4],
    drive: f32,
    revs: f32,
    lateral_force: f32,
    gear: i32,
    basis: [V4; 4],
    inertia: V4,
}

#[derive(Clone, Default)]
struct HandbrakeTurn {
    charge: f32,
    direction: f32,
    last_ms: Option<u32>,
}

impl HandbrakeTurn {
    fn update(&mut self, now: u32, dt: f32, steer: f32, brake: bool) -> f32 {
        let direction = if steer.abs() > 0.05 { steer.signum() } else { 0.0 };
        let elapsed = self.last_ms.replace(now)
            .map_or(dt, |last| (now.wrapping_sub(last) as f32 * 0.001).max(dt));
        // During an unsupported wheel step the car still moves in game time.
        // Age the existing pulse, without charging it for unobserved input.
        self.charge = (self.charge - (elapsed - dt) / 2.1).max(0.0);
        if self.charge == 0.0 || direction * self.direction < 0.0 {
            self.charge = 0.0;
            self.direction = 0.0;
        }
        if brake {
            self.charge = (self.charge + 12.0 * dt).min(1.0);
        } else {
            self.charge = (self.charge - dt / 2.1).max(0.0);
        }
        if self.charge > 0.0 && self.direction == 0.0 {
            self.direction = direction;
        }
        self.charge
    }
}

#[derive(Clone)]
pub struct Car {
    tyres: [Tyre; 4],
    treads: [Tread; 4],
    compression: [f32; 4],
    grounded: [bool; 4],
    gearbox: Transmission,
    handling: Handling,
    suspension: Option<SuspensionProfile>,
    steering_scale: f32,
    clock_origin: Option<u32>,
    handbrake_turn: HandbrakeTurn,
}

impl Car {
    fn new(mass: f32, profile: Profile, drive_bias: [f32; 2]) -> Self {
        let ratios = vehicles::gear_ratios(profile.gears);
        Self {
            tyres: std::array::from_fn(|i| Tyre::new(i < 2, 0.25)),
            treads: [Tread::default(); 4],
            compression: [f32::NAN; 4],
            grounded: [false; 4],
            gearbox: Transmission::default(),
            suspension: None,
            steering_scale: 1.0,
            clock_origin: None,
            handbrake_turn: HandbrakeTurn::default(),
            handling: Handling {
                mass,
                drag: 0.0,
                percent_submerged: 75,
                buoyancy: 0.0,
                centre_of_mass: [0.0; 3],
                drive_bias,
                gears: profile.gears,
                drive_force: profile.drive_force * profile.reference_mass / mass,
                drive_inertia: profile.drive_inertia,
                drive_max_velocity: profile.top_mps,
                max_flat_velocity: profile.top_mps * 1.2,
                gear_ratios: ratios,
                brake_force: profile.brake,
                brake_bias: [1.04, 0.96],
                handbrake: 0.7,
                steering_lock: 30_f32.to_radians(),
                traction_max: profile.traction,
                traction_min: profile.slide_traction,
                traction_lateral: 18_f32.to_radians(),
                traction_spring_delta_max: 0.15,
                traction_bias: [1.02, 0.98],
                suspension_force: profile.spring,
                suspension_comp_damp: profile.compression_damping,
                suspension_rebound_damp: profile.rebound_damping,
                suspension_upper: 0.27,
                suspension_lower: -0.19,
                suspension_raise: 0.0,
                suspension_bias: [1.0, 1.0],
                damage: [0.0; 4],
                seat_offset: 0.0,
                value: 0,
                model_flags: 0,
                handling_flags: 0,
            },
        }
    }

    fn step(&mut self, frame: &Frame, pace: f32) -> Option<Result> {
        let mut inertia = frame.inertia;
        let mut spin = frame.spin;
        if let Some(profile) = self.suspension {
            // Match the change in spring load over a fraction of the SA
            // stroke. Scale damping with sqrt(k) for the same damping ratio.
            let stroke = frame.contacts.iter().map(|c| c.travel).sum::<f32>() * 0.25;
            if !(0.02..=2.0).contains(&stroke) {
                return None;
            }
            let stiffness_scale = (profile.stroke / stroke).clamp(0.4, 2.5);
            self.handling.suspension_force = profile.spring * stiffness_scale;
            self.handling.suspension_comp_damp =
                profile.compression_damping * stiffness_scale.sqrt();
            self.handling.suspension_rebound_damp =
                profile.rebound_damping * stiffness_scale.sqrt();
            self.handling.suspension_bias = profile.bias;
            for axis in 0..3 {
                inertia[axis] *= profile.inertia_ratio[axis];
                spin[axis] += frame.native_spin_loss[axis];
            }
        }
        let h = &self.handling;
        let dt = frame.dt;
        let dot = |a: V4, b: V4| a[..3].iter().zip(b).map(|(x, y)| x * y).sum::<f32>();
        let speed = dot(frame.velocity, frame.basis[1]);
        if frame.reseed_contacts & RESEED_WHEELS != 0 {
            // Native sleeping/boarding or a rejected road support can skip
            // frames. No old hold anchor or pedal force belongs to this step.
            self.treads = [Tread::default(); 4];
            for (tyre, ground) in self.tyres.iter_mut().zip(&frame.contacts) {
                tyre.slip = 0.0;
                tyre.rate = -speed / ground.radius;
            }
        }
        let handbrake = frame.handbrake != 0;
        let quicker = QUICKER_DRIVING.load(Ordering::Relaxed);
        let road_speed = (h.drive_max_velocity * 0.48).clamp(4.0, 20.0);
        let steer_fraction = frame.steer * self.steering_scale / h.steering_lock;
        let turn_demand = ((steer_fraction.abs() - 0.10) / 0.64).clamp(0.0, 1.0);
        let road_turn = ((speed / road_speed - 0.45) / 0.55).clamp(0.0, 1.0) * turn_demand;
        // Carry the handbrake's cornering response through the release.
        // The normal rear brake command still follows the button directly.
        if frame.reseed_contacts & RESET_DRIVER != 0 || speed <= 1.0 {
            self.handbrake_turn = HandbrakeTurn::default();
        }
        let handbrake_charge = self.handbrake_turn.update(
            frame.now_ms, dt, steer_fraction, quicker && handbrake && speed > road_speed * 0.35,
        );
        let assist_gravity = if quicker { frame.gravity_scale } else { 0.0 };
        let road_assist = road_turn * assist_gravity;
        let handbrake_assist = road_assist.max(
            0.55 * turn_demand * ((speed - 3.0) / 9.0).clamp(0.0, 1.0) * assist_gravity
        ) * handbrake_charge;
        let outward_slip = dot(frame.velocity, frame.basis[0]) * frame.steer.signum()
            / speed.abs().max(5.0);
        let drift = ((outward_slip - 0.13) / 0.26).clamp(0.0, 1.0);
        // A long chassis takes more distance to turn. Let it scrub some
        // speed at high lock, in proportion to its yaw inertia per kg.
        let yaw_radius_squared = inertia[2] / h.mass;
        let momentum_retention = 1.0 - 0.90 * ((yaw_radius_squared - 3.8) / 4.0).clamp(0.0, 1.0);
        let corner_grip = if quicker {
            let road_speed = ((speed.abs() - 7.0) / 14.0).clamp(0.0, 1.0);
            let street_speed = ((speed.abs() - 3.0) / 6.0).clamp(0.0, 1.0)
                * ((20.0 - speed.abs()) / 10.0).clamp(0.0, 1.0);
            let turn = turn_demand;
            // SA's rear-heavy chassis can leave only a quarter of the load
            // on the steering axle (Savanna). Compensate at the front tyres
            // while turning and keep the actual mass distribution and roll.
            let front_share = frame.contacts[0].load_share + frame.contacts[1].load_share;
            let light_front = ((0.46 - front_share) / 0.22).clamp(0.0, 1.0);
            (0.18 * road_speed + 0.14 * street_speed
                + 0.30 * light_front * road_speed.max(street_speed)) * turn
        } else {
            0.0
        };
        let resting_on_level_ground = (frame.brake > 0.0 || handbrake)
            && frame.gas.abs() < 0.01
            // SA has already applied its downward impulse before this call.
            && frame.velocity[0].hypot(frame.velocity[1]) < 0.15
            && frame.contacts.iter().filter(|c| c.supported != 0).count() >= 3
            && frame
                .contacts
                .iter()
                .all(|c| c.supported == 0 || c.normal[2] > 0.997);
        let mut wheel_speeds = [0.0; 4];
        let mut powered_count = 0;
        let mut grounded = 0;
        for (i, ground) in frame.contacts.iter().enumerate() {
            if h.drive_bias[i / 2] > 0.0 {
                wheel_speeds[powered_count] = -self.tyres[i].rate * ground.radius;
                powered_count += 1;
                grounded += usize::from(ground.supported != 0);
            }
        }
        let controls = Controls {
            throttle: frame.gas,
            brake: frame.brake,
            handbrake,
            ..Controls::default()
        };
        let drive = self.gearbox.process(
            h, controls, &wheel_speeds[..powered_count], speed, grounded, dt, frame.now_ms,
        ) * if frame.nitro != 0 { 2.0 } else { 1.0 };
        let body = Body {
            inv_inertia: inertia.map(f32::recip),
            inv_mass: h.mass.recip(),
            m: frame.basis,
            vel: frame.velocity,
            ang: spin,
        };
        let mut calls = WheelCalls {
            h,
            mass: Mass {
                mass: h.mass,
                inv_mass: body.inv_mass,
                inv_inertia: body.inv_inertia,
                m: body.m,
            },
            struts: [Strut::default(); 4],
            treads: [Tread::default(); 4],
            axes: [[[0.0; 4]; 2]; 4],
            base: self.tyres.clone(),
            tyres: self.tyres.clone(),
            loads: [0.0; 4],
            lateral: [0.0; 4],
            lateral_scale: [1.0; 4],
            momentum_retention,
        };
        let mut wheels = [Wheel::default(); 4];
        for (i, ground) in frame.contacts.iter().enumerate() {
            let front = i < 2;
            let axle = usize::from(!front);
            let supported = ground.supported != 0;
            let powered = if h.drive_bias[axle] > 0.0 {
                tyre::flags::POWERED() | tyre::flags::DRIVEN()
            } else {
                0
            };
            let flags = powered | if supported { tyre::flags::GROUNDED } else { 0 };
            let compression = ground.compression * ground.travel;
            // A new face at a ramp seam or a landing has no previous spring
            // length on that support. Do not treat free extension as load.
            let previous = if frame.reseed_contacts & RESEED_WHEELS == 0 && self.grounded[i] {
                self.compression[i]
            } else {
                compression
            };
            self.compression[i] = compression;
            self.grounded[i] = supported;
            let wheel = &mut wheels[i];
            wheel.flags = u32::from(supported);
            wheel.slip = self.tyres[i].slip;
            wheel.timer = self.treads[i].no_brake;
            let rear_brake = handbrake && !front;
            // Once the rear is free, shift the extra response rearwards
            // if the car is rotating faster than its travel direction.
            let drift_balance = if front { 1.0 - drift } else { 1.0 + 0.50 * drift };
            let handbrake_gain = if rear_brake { 0.0 } else { 0.48 * handbrake_assist * drift_balance };
            calls.lateral_scale[i] += 0.16 * road_assist + handbrake_gain;
            let demand = if rear_brake {
                0.0
            } else {
                drive * h.drive_bias[axle]
            };
            // Braking can wind up the IV tyre contact anchors. On level
            // ground a stopped car has no static grade load to retain.
            // Rebase the anchors while the suspension settles.
            if resting_on_level_ground || (supported && !previous.is_finite()) {
                self.treads[i].anchor = ground.point;
            }
            self.treads[i] = Tread {
                steer: if front {
                    // Retain lock through junction speeds; narrow it as
                    // the car reaches the faster part of its road range.
                    let road = ((speed.abs() - 16.0) / 17.0).clamp(0.0, 1.0);
                    let limit = if quicker { 1.40 - 0.57 * road } else { 1.25 };
                    let lock = h.steering_lock * limit;
                    (frame.steer * self.steering_scale).clamp(-lock, lock)
                } else {
                    0.0
                },
                drive: demand,
                brake: frame.brake * h.brake_force * h.brake_bias[axle]
                    + if rear_brake { h.handbrake } else { 0.0 },
                drive_prev: self.treads[i].drive,
                brake_prev: self.treads[i].brake,
                ..self.treads[i]
            };
            calls.base[i].flags = flags;
            calls.base[i].load_share = ground.load_share;
            // Preserve the donor's traction during small corrections.
            // Add front authority only through a deliberate road turn.
            if front { calls.base[i].grip *= 1.0 + corner_grip; }
            calls.treads[i] = self.treads[i];
            calls.struts[i] = Strut {
                spring: Spring {
                    front,
                    share: ground.load_share,
                    travel: ground.travel,
                    // Keep the SA ride-height reference. The original IV
                    // rest point assumes the IV wheel and chassis geometry.
                    rest: ground.travel * 0.36,
                    limit: ground.travel,
                },
                x: compression,
                prev: if previous.is_finite() {
                    previous
                } else {
                    compression
                },
                axis: [0.0, 0.0, 1.0, 0.0],
                contact: ground.point,
                start: ground.point,
                normal: ground.normal,
                on_entity: false,
                damage: 1000.0,
                health: 1000.0,
                flags,
                drop: 0.0,
            };
        }

        // Couple each axle's two planted struts. This adds resistance
        // to opposite wheel travel (body roll) without stiffening heave or
        // pitch. Wheel loads and available stroke remain chassis-specific.
        for axle in [0, 2] {
            if frame.contacts[axle..axle + 2].iter().all(|c| c.supported != 0 && c.normal[2] > 0.85) {
                let (left, right) = calls.struts[axle..axle + 2].split_at_mut(1);
                let (left, right) = (&mut left[0], &mut right[0]);
                let transfer = |l: f32, r: f32| {
                    (0.17 * (l - r)).clamp(
                        -l.min(right.spring.travel - r).max(0.0),
                        r.min(left.spring.travel - l).max(0.0),
                    )
                };
                let current = transfer(left.x, right.x);
                let previous = transfer(left.prev, right.prev);
                left.x += current;
                right.x -= current;
                left.prev += previous;
                right.prev -= previous;
            }
        }

        // CPhysical::ProcessControl already applied SA gravity before the
        // suspension call. Its 0.008 speed units per 1/50 s step convert to
        // 20 m/s squared. Add that impulse back inside the IV integrator, which
        // then applies its own gravity to this experimental chassis.
        let gravity_compensation = if delirium::ragdoll::gravity_enabled() {
            let scale = frame.gravity_scale.clamp(delirium::ragdoll::LOW_GRAVITY_SCALE, 1.0);
            let native = 0.008 * 50.0 * 50.0 / pace.powi(2);
            let wheel_gravity = wheelstep::GRAVITY()[2];
            let support_gravity = if frame.contacts.iter().any(|c| c.supported != 0) {
                wheel_gravity
            } else {
                wheel_gravity / pace.powi(2)
            };
            native * scale + support_gravity * scale - wheel_gravity
        } else {
            // Native ApplyGravity is skipped, and the fixed gravity inside
            // wheelstep is cancelled without changing spring or tyre forces.
            -wheelstep::GRAVITY()[2]
        };
        let mut body_torque = self.suspension.map_or([0.0; 4], |profile| {
            profile.torque(spin, &frame.basis, inertia, dt)
        });
        let (planted, normal_sum) = frame.contacts.iter()
            .filter(|c| c.supported != 0 && c.normal[2] > 0.85)
            .fold((0, [0.0; 4]), |(count, sum), c| {
                (count + 1, std::array::from_fn(|i| sum[i] + c.normal[i]))
            });
        if planted >= 2 {
            let roll_axis = frame.basis[1];
            let side = frame.basis[0];
            // Keep measuring lean when the inner tyres lift. Dropping the
            // correction at two planted tyres lets the roll build unchecked.
            let lean = dot(side, normal_sum) / dot(normal_sum, normal_sum).sqrt();
            let excess_lean = (lean.abs() - 0.17).max(0.0).copysign(lean);
            // Keep the lean under load. Dampen its momentum a little
            // more so the body follows a change of direction sooner.
            let roll_acceleration = 2.2 * lean
                + (12.0 + 26.0 * road_assist) * excess_lean
                - (1.0 + 0.5 * road_assist) * dot(spin, roll_axis);
            for axis in 0..3 {
                body_torque[axis] += roll_axis[axis] * inertia[1] * roll_acceleration;
            }
            // With fewer road samples per second, the rear tyres can take
            // longer to recover from a handbrake pulse. Dampen yaw once
            // the button is released and the body is sliding wide.
            if quicker && !handbrake {
                let coarse_frame = ((dt / pace - 0.03) / 0.02).clamp(0.0, 1.0);
                let wide_slide = ((outward_slip - 0.28) / 0.42).clamp(0.0, 1.0);
                let yaw_axis = frame.basis[2];
                let steady = (1.0 - turn_demand * 4.0).max(0.0)
                    * ((speed.abs() - 6.0) / 11.0).clamp(0.0, 1.0)
                    * (1.0 - (outward_slip.abs() / 0.30).min(1.0));
                let yaw_damping = 3.0 * steady + 12.0 * coarse_frame * wide_slide * handbrake_charge;
                let correction = -dot(spin, yaw_axis) * yaw_damping * inertia[2];
                for axis in 0..3 {
                    body_torque[axis] += yaw_axis[axis] * correction;
                }
            }
        }
        let state = wheelstep::integrate(
            &body,
            [0.0, 0.0, gravity_compensation * h.mass, 0.0],
            body_torque,
            &mut wheels,
            dt,
            &mut calls,
        );
        for (i, ground) in frame.contacts.iter().enumerate() {
            let contact = ground.supported != 0;
            self.treads[i].no_brake = wheels[i].timer;
            let v_long = wheelstep::hold(
                h,
                &calls.mass,
                &calls.struts[i],
                &mut self.treads[i],
                &mut calls.axes[i],
                AXLE,
                state.vel,
                state.ang,
                dt,
            );
            let tread = self.treads[i];
            let tyre = &mut self.tyres[i];
            if contact {
                tyre.slip = state.slip[i];
                tyre.flags = calls.tyres[i].flags;
                tyre.skid = calls.tyres[i].skid;
            }
            let gear = self.gearbox.gear;
            let limit = -h.max_flat_velocity / h.gear_ratios[gear.max(0) as usize] / ground.radius;
            tyre::spin(
                tyre,
                &Contact {
                    v_long,
                    drive: tread.drive,
                    brake: tread.brake,
                },
                contact,
                h.mass,
                ground.radius,
                limit,
                if gear > 0 { 1.0 } else { -1.0 },
                dt,
            );
        }
        let result = Result {
            velocity: state.vel,
            spin: state.ang,
            spring_force: calls.loads,
            drive,
            revs: self.gearbox.revs,
            lateral_force: calls.lateral.iter().sum(),
            gear: i32::from(self.gearbox.gear),
            basis: state.m,
            inertia,
        };
        result
            .velocity
            .into_iter()
            .chain(result.spin)
            .chain(result.spring_force)
            .chain([result.drive, result.revs, result.lateral_force])
            .chain(result.basis.into_iter().flatten())
            .all(f32::is_finite)
            .then_some(result)
    }
}

struct WheelCalls<'a> {
    h: &'a Handling,
    mass: Mass,
    struts: [Strut; 4],
    treads: [Tread; 4],
    axes: [[V4; 2]; 4],
    base: [Tyre; 4],
    tyres: [Tyre; 4],
    loads: [f32; 4],
    lateral: [f32; 4],
    lateral_scale: [f32; 4],
    momentum_retention: f32,
}

impl wheelstep::Calls for WheelCalls<'_> {
    fn advance(&mut self, m: &[V4; 4], vel: V4, ang: V4, h: f32) -> [V4; 4] {
        wheelstep::advance(m, vel, ang, h)
    }

    fn suspension(&mut self, i: usize, m: &[V4; 4], vel: V4, _: V4, t: &Times) -> Out {
        let result = wheelstep::suspension(self.h, &self.mass, &self.struts[i], m, vel, t);
        self.loads[i] = result.value.unwrap_or(0.0);
        result
    }

    fn tyre(
        &mut self,
        i: usize,
        load: f32,
        slip: f32,
        timer: f32,
        m: &[V4; 4],
        vel: V4,
        ang: V4,
        t: &Times,
    ) -> Out {
        let strut = &self.struts[i];
        let cache = &mut self.axes[i];
        let mut tyre = self.base[i].clone();
        let tread = Tread {
            no_brake: timer,
            ..self.treads[i]
        };
        let (forward, side) = wheelstep::axes(cache, AXLE, strut.normal, strut.flags, &self.mass.m, tread.steer);
        let mut roll_acceleration = 0.0;
        let mut result = wheelstep::tyre(
            self.h,
            &self.mass,
            strut,
            &tread,
            load,
            slip,
            m,
            vel,
            ang,
            t,
            |_| (forward, side),
            |input| {
                let mut force = tyre::tyre_force(self.h, &mut tyre, self.mass.mass, self.mass.inv_mass, input);
                let assistance = self.lateral_scale[i] - 1.0;
                if assistance > 0.0 && input.along.abs() > 3.0 {
                    // Project the assist towards the contact's lateral
                    // direction of travel to limit added speed loss.
                    let contact_speed = ((input.along.abs() - 3.0) / 5.0).min(1.0);
                    let lateral = force[0] * assistance * contact_speed;
                    let slip = (input.across / input.along).clamp(-1.5, 1.5);
                    let longitudinal = -lateral * slip * self.momentum_retention;
                    force[0] += lateral;
                    force[1] += longitudinal;
                    // Apply most of the assist through the centre of mass
                    // in roll. The original contact force still rolls the
                    // body through its own suspension at full leverage.
                    let extra: [f32; 3] = std::array::from_fn(|axis|
                        side[axis] * lateral + forward[axis] * longitudinal);
                    let arm: [f32; 3] = std::array::from_fn(|axis|
                        strut.contact[axis] - self.mass.m[3][axis]);
                    let torque = [
                        arm[1] * extra[2] - arm[2] * extra[1],
                        arm[2] * extra[0] - arm[0] * extra[2],
                        arm[0] * extra[1] - arm[1] * extra[0],
                    ];
                    let roll_torque = torque.iter().zip(m[1]).map(|(t, f)| t * f).sum::<f32>();
                    roll_acceleration = 0.9 * roll_torque * self.mass.inv_inertia[1];
                }
                force
            },
        );
        for axis in 0..3 {
            result.ang_acc[axis] -= roll_acceleration * m[1][axis];
        }
        self.lateral[i] = result.push[..3].iter().zip(m[0]).map(|(x, y)| x * y).sum();
        self.tyres[i] = tyre;
        result
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_vehicle_lab_abi() -> u32 {
    10
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_driver_limits(car: *const Car, limits: *mut [f32; 2]) -> i32 {
    let (Some(car), Some(limits)) = (unsafe { car.as_ref() }, unsafe { limits.as_mut() }) else {
        return 0;
    };
    *limits = [car.steering_scale, car.handling.traction_min];
    1
}

/// Body advance in the same units as the IV wheel integrator.
/// TU8 0x824FBE40 calls the axis-angle rotation at 0x822F3180.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_turn(
    basis: *const [V4; 4],
    spin: *const V4,
    seconds: f32,
    output: *mut [V4; 4],
) -> i32 {
    let (Some(basis), Some(spin), Some(output)) = (
        unsafe { basis.as_ref() },
        unsafe { spin.as_ref() },
        unsafe { output.as_mut() },
    ) else {
        return 0;
    };
    if !delirium_data::kv::open()
        || !(0.0..=0.12).contains(&seconds)
        || !basis
            .iter()
            .flatten()
            .chain(spin)
            .all(|value| value.is_finite())
    {
        return 0;
    }
    let turned = wheelstep::advance(basis, [0.0; 4], *spin, seconds);
    if !turned.iter().flatten().all(|value| value.is_finite()) {
        return 0;
    }
    *output = turned;
    1
}

#[unsafe(no_mangle)]
pub extern "C" fn sa_vehicle_lab_create(
    mass: f32,
    model: u32,
    drive_type: u32,
    length: f32,
    height: f32,
    stock_flat_mps: f32,
    stock_gears: u32,
    stock_steering_deg: f32,
) -> *mut Car {
    if !delirium_data::kv::open()
        || !(40.0..=25000.0).contains(&mass)
        || !(0.35..=20.0).contains(&length)
        || !(0.15..=8.0).contains(&height)
    {
        return std::ptr::null_mut();
    }
    let mut profile = vehicle_profiles::for_model(model, mass, length, height);
    // Anchor the donor transmission to the SA model's speed hierarchy.
    // Car::new adds IV's flat-speed headroom before the clock conversion.
    if (4.0..=100.0).contains(&stock_flat_mps) {
        profile.top_mps = stock_flat_mps;
    }
    if (1..=6).contains(&stock_gears) {
        profile.gears = stock_gears as u8;
    }
    let drive_bias = match drive_type {
        70 => [1.0, 0.0], // F
        82 => [0.0, 1.0], // R
        52 => [1.0, 1.0], // 4
        _ => return std::ptr::null_mut(),
    };
    let mut car = Car::new(mass, profile, drive_bias);
    if let Some(reference) = iv_tuning(model) {
        car.suspension = Some(SuspensionProfile::from_handling(reference));
        let sa_top = car.handling.drive_max_velocity;
        let sa_flat = car.handling.max_flat_velocity;
        car.handling = reference.clone();
        let h = &mut car.handling;
        h.mass = mass;
        h.drive_max_velocity = sa_top;
        h.max_flat_velocity = sa_flat;
        // Transmission::process divides by powered wheels and the average
        // bias of those wheels is one. Redistribute engine effort across the
        // SA axles. Scaling by wheel count here would halve or double it.
        if drive_bias.contains(&0.0) || reference.drive_bias.contains(&0.0) {
            h.drive_bias = drive_bias;
        }
        if (5.0..=65.0).contains(&stock_steering_deg) {
            car.steering_scale = h.steering_lock / stock_steering_deg.to_radians();
        }
    }
    if model == 445 {
        car.suspension = ADMIRAL.get().copied().or(car.suspension);
    }
    Box::into_raw(Box::new(car))
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_step(
    car: *mut Car,
    frame: *const Frame,
    result: *mut Result,
) -> i32 {
    let (Some(car), Some(frame), Some(result)) =
        (unsafe { car.as_mut() }, unsafe { frame.as_ref() }, unsafe {
            result.as_mut()
        })
    else {
        return 0;
    };
    // CTimer caps the native step at three 20 ms ticks. The IV wheel
    // integrator already subdivides that interval, so a slow frame still
    // receives the same controls and a complete spring/damper update.
    if !(0.001..=MAX_SA_FRAME_SECONDS).contains(&frame.dt) || frame.mass != car.handling.mass {
        return 0;
    }
    let mut next = car.clone();
    let pace = if QUICKER_DRIVING.load(Ordering::Relaxed) { VEHICLE_PACE } else { 1.0 };
    // A change of time units preserves the donor's steering geometry,
    // gear spacing and spring response. Native collisions stay in SA time.
    let origin = *next.clock_origin.get_or_insert(frame.now_ms);
    let elapsed = frame.now_ms.wrapping_sub(origin);
    let mut simulation = *frame;
    simulation.dt *= pace;
    simulation.now_ms = if pace == 1.0 { elapsed } else { ((u64::from(elapsed) * 13) / 10) as u32 };
    simulation.velocity = frame.velocity.map(|v| v / pace);
    simulation.spin = frame.spin.map(|v| v / pace);
    simulation.native_spin_loss = frame.native_spin_loss.map(|v| v / pace);
    let Some(mut output) = next.step(&simulation, pace) else {
        return 0;
    };
    output.velocity = output.velocity.map(|v| v * pace);
    output.spin = output.spin.map(|v| v * pace);
    *result = output;
    *car = next;
    1
}

#[unsafe(no_mangle)]
pub unsafe extern "C" fn sa_vehicle_lab_destroy(car: *mut Car) {
    if !car.is_null() {
        drop(unsafe { Box::from_raw(car) });
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn handbrake_tap_survives_skipped_wheel_steps() {
        let mut turn = HandbrakeTurn::default();
        assert!(turn.update(100, 0.02, 0.0, true) > 0.20);
        let after_gap = turn.update(200, 0.02, 0.3, false);
        assert!(after_gap > 0.15 && after_gap < 0.24);
        assert_eq!(turn.update(220, 0.02, -0.3, false), 0.0);
        turn.update(240, 0.02, -0.3, true);
        assert_eq!(turn.update(2600, 0.02, -0.3, false), 0.0);
    }

    #[test]
    #[ignore = "requires the owned GTA IV program imported by the installer"]
    fn counterparts_use_distinct_iv_handling_and_keep_sa_chassis() {
        let root = std::path::PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        let program = std::fs::read(root.join("DeliriumSA-local-v3/iv-program.bin")).unwrap();
        assert!(delirium_data::kv::unlock_program(&program));
        let bytes = std::fs::read(root.join("DeliriumSA-local-v3/iv-vehicle-handling.dat")).unwrap();
        let table = vehicles::read_handling(&String::from_utf8_lossy(&bytes));
        let ide = std::fs::read_to_string(root.join("data/vehicles.ide")).unwrap();
        let mut count = 0;
        for line in ide.lines() {
            let mut columns = line.split(',').map(str::trim);
            let Some(model) = columns.next().and_then(|s| s.parse::<u32>().ok()) else { continue };
            if !matches!(columns.nth(2), Some("car" | "mtruck" | "quad")) || [432, 486, 564].contains(&model) {
                continue;
            }
            count += 1;
            if let Some(key) = vehicle_counterparts::counterpart(model) {
                let h = table.get(key).unwrap_or_else(|| panic!("{model}: missing {key}"));
                assert!(verified_handling(h), "{model}: invalid {key}: {h:?}");
                assert!(!vehicle_counterparts::SPECIAL_MODELS.contains(&model));
            } else {
                assert!(vehicle_counterparts::SPECIAL_MODELS.contains(&model), "missing {model}");
            }
        }
        assert_eq!(count, 146);
        let _ = IV_HANDLING.set(table);
        let car = |model, drive| {
            let ptr = sa_vehicle_lab_create(1400.0, model, drive, 4.0, 1.3, 50.0, 5, 30.0);
            assert!(!ptr.is_null());
            unsafe { Box::from_raw(ptr) }
        };
        let comet = car(480, 52);
        let cheetah = car(415, 82);
        let sentinel = car(405, 82);
        assert_eq!(cheetah.handling.drive_bias, [0.0, 1.0]);
        assert_eq!(comet.handling.drive_bias, iv_tuning(480).unwrap().drive_bias);
        assert_eq!(cheetah.handling.mass, 1400.0);
        assert!((cheetah.handling.max_flat_velocity - 60.0).abs() < 0.001);
        assert_ne!(cheetah.handling, comet.handling);
        assert_ne!(sentinel.handling, comet.handling);
        for (model, drive) in [(405, 82), (415, 82), (445, 82), (480, 52), (524, 82), (571, 82)] {
            let adapted = car(model, drive);
            let h = &adapted.handling;
            assert!((h.max_flat_velocity - 60.0).abs() < 0.001, "{model}");
            assert!((h.drive_max_velocity - 50.0).abs() < 0.001, "{model}");
            if let Some(reference) = iv_tuning(model) {
                let expected = reference.drive_force;
                for drive in [70, 82, 52] {
                    let other = car(model, drive);
                    let bias = other.handling.drive_bias;
                    let driven = bias.iter().filter(|&&b| b > 0.0).count();
                    let total = other.handling.drive_force * bias.iter().sum::<f32>() / driven as f32;
                    assert!((total - expected).abs() < 0.00001, "{model} {drive}");
                }
                assert!((h.max_flat_velocity / h.gear_ratios[0] - 60.0 / reference.gear_ratios[0]).abs() < 0.001, "{model}");
                if h.gears > 1 {
                    assert!((h.max_flat_velocity / h.gear_ratios[1] - 60.0 / reference.gear_ratios[1]).abs() < 0.001, "{model}");
                }
                assert!((h.traction_max / reference.traction_max - 1.0).abs() < 0.001, "{model}");
                assert!((h.traction_min / reference.traction_min - 1.0).abs() < 0.001, "{model}");
            }
        }
    }

    #[test]
    fn suspension_profile_scales_inertia_and_caps_damping_at_rest() {
        let json = serde_json::json!({
            "model": "admiral",
            "collision_dimensions_m": [2.0, 4.0, 1.0],
            "inertia_per_kg": [1.0, 0.5, 1.5],
            "fragment_damping": [0.0, 0.0, 0.0, 0.2, 0.0, 0.5],
            "spring_axle_bias": [0.8, 1.2],
            "upper_stop_m": 0.2,
            "lower_stop_m": -0.2,
            "spring_force": 2.0,
            "compression_damping": 0.3,
            "rebound_damping": 0.6,
        });
        let profile = SuspensionProfile::read(json.to_string().as_bytes()).unwrap();
        assert_eq!(profile.inertia_ratio, [12.0 / 17.0, 6.0 / 5.0, 18.0 / 20.0]);
        let basis = [
            [0.0, 1.0, 0.0, 0.0],
            [-1.0, 0.0, 0.0, 0.0],
            [0.0, 0.0, 1.0, 0.0],
            [0.0, 0.0, 0.0, 1.0],
        ];
        let torque = profile.torque([2.0, 0.001, 0.0, 0.0], &basis, [2.0, 3.0, 4.0, 1.0], 0.02);
        assert!((torque[0] + 6.9).abs() < 1e-5);
        assert!((torque[1] + 0.1).abs() < 1e-5);
        assert_eq!(torque[2], 0.0);
    }

    #[test]
    #[ignore = "requires the owned GTA IV program imported by the installer"]
    fn transmission_shifts_on_a_rolling_admiral() {
        let root = std::path::PathBuf::from(std::env::var_os("SA_DELIRIUM_TEST_ROOT").unwrap());
        let program = std::fs::read(root.join("DeliriumSA-local-v3/iv-program.bin")).unwrap();
        assert!(delirium_data::kv::unlock_program(&program));

        let mut car = Car::new(
            1650.0,
            vehicle_profiles::for_model(445, 1650.0, 4.8, 1.5),
            [1.0, 0.0],
        );
        let mut previous_gear = 1;
        let mut shifts = Vec::new();
        let radius = 0.32;
        let ground = |x, y| Ground {
            point: [x, y, 0.0, 0.0],
            normal: [0.0, 0.0, 1.0, 0.0],
            travel: 0.46,
            compression: 0.36,
            radius,
            load_share: 0.25,
            supported: 1,
        };
        let mut frame = Frame {
            basis: [
                AXLE,
                [0.0, 1.0, 0.0, 0.0],
                [0.0, 0.0, 1.0, 0.0],
                [0.0, 0.0, 0.6, 1.0],
            ],
            velocity: [0.0; 4],
            spin: [0.0; 4],
            native_spin_loss: [0.0; 4],
            inertia: [3850.0, 3850.0, 3850.0, 1.0],
            contacts: [
                ground(-0.8, 1.4),
                ground(0.8, 1.4),
                ground(-0.8, -1.4),
                ground(0.8, -1.4),
            ],
            mass: 1650.0,
            dt: 0.01,
            gas: 1.0,
            brake: 0.0,
            steer: 0.0,
            now_ms: 0,
            reseed_contacts: 0,
            handbrake: 0,
            nitro: 0,
            gravity_scale: 1.0,
        };
        // Supply a rolling road speed and matching wheel speed. The short
        // in-game launch/brake fixture cannot cover the full gear range.
        for tick in 0..=1000 {
            let speed = tick as f32 * 0.045;
            frame.velocity[1] = speed;
            frame.now_ms = tick * 10;
            for tyre in &mut car.tyres {
                tyre.rate = -speed / radius;
            }
            let result = car.step(&frame, 1.0).expect("non-finite wheel response");
            assert!((1..=5).contains(&result.gear));
            if previous_gear != result.gear {
                shifts.push((result.gear, speed));
                previous_gear = result.gear;
            }
        }
        assert!(shifts.first().is_some_and(|shift| shift.0 == 2));
        assert!(shifts.iter().any(|shift| shift.0 >= 3));
        println!("admiral_iv_upshifts_gear_and_mps={shifts:?}");

        // The live boundary advances the same transmission over 1.3x
        // virtual time and converts motion back to the native game's units.
        frame.reseed_contacts = RESEED_WHEELS;
        frame.now_ms = 10_000;
        let mut direct = car.clone();
        let mut virtual_frame = frame;
        virtual_frame.now_ms = 0;
        virtual_frame.dt *= VEHICLE_PACE;
        virtual_frame.velocity = frame.velocity.map(|v| v / VEHICLE_PACE);
        virtual_frame.spin = frame.spin.map(|v| v / VEHICLE_PACE);
        virtual_frame.native_spin_loss = frame.native_spin_loss.map(|v| v / VEHICLE_PACE);
        let expected = direct.step(&virtual_frame, VEHICLE_PACE).unwrap();
        let mut paced = car.clone().step(&frame, 1.0).unwrap();
        assert_eq!(unsafe { sa_vehicle_lab_step(&mut car, &frame, &mut paced) }, 1);
        assert_eq!(paced.velocity, expected.velocity.map(|v| v * VEHICLE_PACE));
        assert_eq!(paced.spin, expected.spin.map(|v| v * VEHICLE_PACE));
        assert_eq!(paced.basis, expected.basis);

        let mut result = car.step(&frame, 1.0).expect("non-finite wheel response");
        frame.dt = MAX_SA_FRAME_SECONDS;
        frame.steer = 0.3;
        frame.handbrake = 1;
        frame.now_ms += 60;
        assert_eq!(unsafe { sa_vehicle_lab_step(&mut car, &frame, &mut result) }, 1);
        assert!(result.velocity[1] > 0.0 && result.spin.iter().all(|spin| spin.abs() < 5.0));
        frame.dt += 0.001;
        assert_eq!(unsafe { sa_vehicle_lab_step(&mut car, &frame, &mut result) }, 0);

        // In free fall, the wheel solver replaces SA's already-applied
        // gravity with the IV equivalent at the same entity-specific scale.
        frame.dt = 0.02;
        frame.gas = 0.0;
        frame.handbrake = 0;
        frame.steer = 0.0;
        for ground in &mut frame.contacts {
            ground.supported = 0;
        }
        let mut falling = |mut car: Car, scale| {
            frame.gravity_scale = scale;
            frame.velocity = [0.0, 0.0, -20.0 * frame.dt * scale, 0.0];
            car.step(&frame, 1.0).unwrap().velocity[2]
        };
        let normal = falling(car.clone(), 1.0);
        let reduced = falling(car, 0.01);
        assert!(normal < 0.0);
        println!("airborne_vehicle_vertical_speed normal={normal} reduced={reduced}");
        assert!((reduced / normal - 0.01).abs() < 0.00001);
    }
}
