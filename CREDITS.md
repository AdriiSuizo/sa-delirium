# Créditos

- **AdriiSuizo** dirige la integración y el proyecto.
- **Izzy / Izzys Mods**, autora de **Lively Vehicle Physics 1.6**. Incluido con su permiso explícito, conservando su ASI, configuración y readme originales. [Publicación de la autora](https://libertycity.net/files/gta-san-andreas/246038-izzys-lively-vehicle-physics-1-6.html).
- **FxsFuncs 1.1 Build 46 (47 fix)**, de Junior_Djjr, con soporte de Ezekiel. ASI original incluido según su permiso para compilaciones, con su readme y créditos. [Página original](https://www.mixmods.com.br/2025/06/fxsfuncs/).
- **Ultimate ASI Loader 10.0.2**, de ThirteenAG y colaboradores. Cargador y API de superposición virtual incluidos bajo licencia MIT. [Proyecto](https://github.com/ThirteenAG/Ultimate-ASI-Loader/tree/v10.0.2)
- **Delirium Physics 0.6.0**, de PVCPC y colaboradores. SDK del preparador, GPLv3 con el permiso adicional incluido en `bootstrap/COPYING`. [Código del SDK](https://github.com/PVCPC/delirium-physics/tree/93eed4d6a0aafa2e84526730ea1f54dab48ac7d4)
- **Delirium Physics**, motor adaptado conservado en `vendor`, con la corrección de pivotes de la versión 0.6.0. Sus fuentes, procedencia y modificaciones están documentadas allí; no incluye los recursos de GTA IV.
- **Large Steal Car 4 / SuperSloppers**, origen y referencia del controlador de combate adaptado y de la investigación nativa. Se conservan sus avisos de procedencia en `vendor/src/lsc4-combat` y `vendor/src/sa-combat`. [Proyecto original](https://github.com/SuperSloppers/large-steal-car-4), [coordinación](https://github.com/AdriiSuizo/lsc4-handoff).
- **MinHook**, de Tsuda Kageyu y colaboradores. Fuentes auxiliares del adaptador bajo su licencia BSD, conservada en `vendor/third-party/minhook/LICENSE.txt`.
- **Xenia**, de Ben Vanik y colaboradores. La constante pública de soporte XEX permite importar sin Internet; procede de `xex_module.cc`, revisión `95a5c3ee250f80c3b9d139658649d9ffb6db3eec`, bajo BSD de tres cláusulas. [Fuente](https://github.com/xenia-project/xenia/blob/95a5c3ee250f80c3b9d139658649d9ffb6db3eec/src/xenia/cpu/xex_module.cc); aviso completo en `licenses/Xenia-BSD-3-Clause.txt`. Los programas y activos de GTA IV siguen procediendo de la ISO del jugador.
- **SilentPatchSA 34.1**, de Silent/CookiePLMonster y colaboradores, bajo MIT. [Proyecto](https://github.com/CookiePLMonster/SilentPatch/releases/tag/1.1-BUILD34.1-SA); licencia conservada en `licenses/SilentPatch-MIT.txt`.
- **plugin-sdk**, de DK22Pac y GTA Community. Referencia de estructuras y formatos. [Proyecto](https://github.com/DK22Pac/plugin-sdk)
- **Open Limit Adjuster**, de ThirteenAG, LINK/2012 y colaboradores, MIT. El backend Steam de almacenamiento de modelos sigue su contrato de construcción, direcciones estables y limpieza nativa; no se copia el ASI de otra versión del juego. Referencia fijada `e454bfaf7edf4d293e533401b0a03330cde80906`; aviso en `licenses/OpenLimitAdjuster-MIT.txt`. [Fuente](https://github.com/GTAmodding/III.VC.SA.LimitAdjuster/blob/e454bfaf7edf4d293e533401b0a03330cde80906/source/ModelStores.cpp).
- **SkyGfx**, de aap y colaboradores. Referencia técnica de la modulación de color PS2. [Proyecto](https://github.com/aap/skygfx)

Desarrollo asistido por OpenAI Codex (familia GPT-6). GTA San Andreas y GTA IV pertenecen a sus titulares.

El perfil portátil clásico incluye los fixes, SkyGfx Extended1.7 de aap y
Junior_Djjr, el timecycle PS2 leído de Steam, Project2DFX, NextGen Remaster Effects
de Ezekiel/Junior_Djjr y Enhance ParticleTXD de Junior_Djjr. Conserva los créditos,
readmes y licencias originales en `Documentation/Original` y
`Documentation/Licenses`. Los plugins se incluyen en el instalador; sus autores
conservan sus derechos y sus términos individuales. La lista de orígenes está en
`installer/portable/source-catalog.json` y el inventario de construcción.
Se incluyen los avisos originales de las dependencias Rust, DXVK (zlib),
Framerate Vigilante y PortableGTA (MIT), plugin-sdk (zlib), 7-Zip y DirectSetup.
RoSA/Proper Player Retex, GTA IV Textures and HQ Roads, el HUD de GTA V y los
extras HD/trainer/Urbanize no forman parte de este perfil.

## Optional local visual data

These references credit locally imported user downloads; they do not grant
redistribution rights. The local payload and generated game archives are not
uploaded to this source repository.

- [HD Skin Pack 299 Skins](https://www.gtainside.com/sanandreas/skins/94757-hd-skin-pack-299-skins-updated/).
- [NeXtGen Remaster Weapons](https://www.mixmods.com.br/2021/05/nextgen-remaster-weapons-pack-de-armas-em-hd/). Visual model/texture import; sound banks are a separate pending connection.
- [Improved Lamps v0.8](https://www.mixmods.com.br/2021/09/improved-lamps-v0-8-postes-remasterizados-em-hd/). Local candidate integrates its model/texture definitions using sparse owner-side data patches.
