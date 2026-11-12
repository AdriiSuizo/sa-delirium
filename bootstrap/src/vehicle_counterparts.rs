//! SA model IDs mapped to the closest four-wheel handling in base GTA IV.
//! The names refer to the owner's handling.dat. No tuning values are embedded.

pub(crate) fn counterpart(model: u32) -> Option<&'static str> {
    Some(match model {
        // Sports and performance coupes.
        402 | 603 => "RUINER",           // Buffalo, Phoenix
        411 => "INFERNUS",
        415 | 451 | 541 => "TURISMO",    // Cheetah, Turismo, Bullet
        429 => "BANSHEE",
        477 | 559 | 587 => "COQUETTE",   // ZR-350, Jester, Euros
        480 => "COMET",
        506 | 602 => "SUPERGT",          // Super GT, Alpha
        533 | 555 => "FELTZER",          // Feltzer, Windsor
        558 => "URANUS",
        560 => "SULTAN",
        562 => "SULTANRS",              // Elegy
        // Passenger cars, wagons and lowriders.
        401 | 436 | 526 | 527 => "FORTUNE",
        404 => "PERENNIAL",
        405 => "SENTINEL",
        409 => "STRETCH",
        410 | 496 | 565 | 589 => "BLISTA",
        412 | 534 | 536 | 567 => "VOODOO",
        419 => "ESPERANT",
        421 => "WASHINGTON",
        426 | 550 => "PREMIER",
        439 => "STALLION",
        442 => "ROMERO",
        445 => "ADMIRAL",
        458 => "SOLAIR",
        466 | 467 | 492 => "EMPEROR",
        474 | 575 | 576 => "PEYOTE",
        475 | 542 => "SABRE",
        479 => "INGOT",
        491 | 517 => "VIRGO",
        504 | 604 => "EMPEROR2",
        507 => "PMP600",
        516 => "FEROCI",
        518 => "BUCCANEER",
        529 | 566 => "WILLARD",
        540 => "VINCENT",
        546 => "INTRUDER",
        547 => "PRIMO",
        549 => "FACTION",
        551 => "MERIT",
        561 => "STRATUM",
        580 => "COGNOS",
        // Four-wheel drives, pickups and vans.
        400 => "LANSTALK",
        413 => "PONY",
        418 | 483 => "MOONBEAM",
        422 | 478 | 535 | 543 | 554 | 600 | 605 => "BOBCAT",
        440 | 482 => "BURRITO",
        459 | 582 => "BURRITO2",
        470 | 495 => "PATRIOT",
        489 | 500 | 505 => "RANCHER",
        552 => "CONTENDER",
        579 => "HUNTLEY",
        // Trucks, utility and services.
        403 | 514 | 515 => "PHANTOM",
        407 | 544 => "FIRETRUK",
        408 => "TRASH",
        414 => "MULE",
        416 => "AMBULANCE",
        420 => "TAXI",
        423 | 588 => "MRTASTY",
        427 | 428 | 528 | 601 => "STOCKADE",
        431 | 437 => "BUS",
        433 | 455 | 578 => "FLATBED",
        438 => "CABBY",
        443 => "PACKER",
        456 => "YANKEE",
        485 | 583 => "AIRTUG",
        490 | 599 => "POLPAT",
        498 | 508 | 609 => "BOXVILLE",
        499 => "BENSON",
        524 => "BIFF",
        530 => "FORKLIFT",
        596 => "POLICE",
        597 | 598 => "POLICE2",
        _ => return None,
    })
}

pub(crate) const SPECIAL_MODELS: &[u32] = &[
    406, 424, 434, 441, 444, 457, 471, 494, 502, 503, 525, 531, 532, 545, 556,
    557, 568, 571, 572, 573, 574, 594,
];
