# Validation 0.3.9 — 2026-11-07

Runtime 19. Two fixes in the game and one change in the installer.

- Waist fold: many stock peds store a bind skeleton bent 14-20 degrees, and the rig took it as neutral. `RestSkeleton.hpp` and `Rig::anatomy` lay each model's own bone lengths along the straight rest directions. Dev game, original models, `cj-uzi` and `ground-shots`: first-second tilt 104-131 degrees before, 8-37 after. HD reference: 8-41.
- Corpse freeze now needs a still head and a lying body, not only a still pelvis.
- `nightstick`, `extras` and impact `Group` passed on original models with empty fault logs.
- Installer, original models: `SoftModels` rewrites skin weights of the owner's ped models from `models/gta3.img` into `modloader/Modelos Suavizados`. Nothing of them ships. From stock Steam `gta3.img` it writes 320 models in about 3 s; the 290 also tested in Adrii's Steam copy are byte-identical.
- Literal EXE on a PC 1.0 US lab copy, original models, empty cache: 106 s, 329 softened models, no HD files, `initialized`, restored.

Not checked: pushes from pedestrians on their own, and the variety of reactions by hit zone and weapon by eye. The installer window with the new option was rendered and checked. In the Steam copy: install 105 s, 320 models, no HD pack, Urbanize kept, saves unchanged, `initialized`.
