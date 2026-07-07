# Third-party notices

The MIT license in the repository root applies to the original `wdwmo`,
`wdwmcd`, and `wdwmosl` code authored for this project. It does **not**
replace, override, or relicense third-party components distributed with or
used by the repository.

The complete license texts referenced below are stored in [`.licenses/`](.licenses/).
Copyright notices must be preserved when the corresponding source or binary
component is redistributed.

## Bundled components

| Component | Repository paths / artifacts | Identified version | License | License file |
|---|---|---:|---|---|
| Dear ImGui | `shared/includes/sources/imgui/` | 1.92.8 | MIT | [`.licenses/Dear-ImGui-MIT.txt`](.licenses/Dear-ImGui-MIT.txt) |
| raw_pdb | `shared/includes/sources/raw_pdb/` | version not declared in the bundled source | BSD 2-Clause | [`LICENSES/raw_pdb-BSD-2-Clause.txt`](.licenses/raw_pdb-BSD-2-Clause.txt) |
| MinHook, including its bundled HDE32/HDE64 portions | `shared/includes/sources/minhook.h`, `shared/includes/libraries/minhook.lib` | version not embedded in the bundled files | BSD 2-Clause-style notices | [`.licenses/MinHook-BSD-2-Clause.txt`](.licenses/MinHook-BSD-2-Clause.txt) |
| BeaEngine | `shared/includes/libraries/beaengine.lib` | exact build not embedded; the current upstream documentation identifies BeaEngine 5.x | LGPL-3.0-or-later | [`.licenses/BeaEngine-LGPL-3.0-or-later.txt`](.licenses/BeaEngine-LGPL-3.0-or-later.txt), [`.licenses/GPL-3.0.txt`](.licenses/GPL-3.0.txt) |
| cpr | `shared/includes/libraries/cpr.lib` | exact upstream version not embedded; the archive contains the Vcpkg source-tree identifier `0ce00c60cd-ac486fb55d` | MIT | [`.licenses/cpr-MIT.txt`](.licenses/cpr-MIT.txt) |
| curl / libcurl | `shared/includes/libraries/libcurl.lib` | 7.86.0-DEV | curl license | [`.licenses/curl.txt`](.licenses/curl.txt) |
| zlib | `shared/includes/libraries/zlib.lib` | 1.2.13 | zlib license | [`.licenses/zlib.txt`](.licenses/zlib.txt) |
| `manual_map_library` function | `shared/includes/sources/ncore/utils.hpp` | unknown, old | MIT | [`.licenses/TheCruZ-SMMI-MIT.txt`](.licenses/TheCruZ-SMMI-MIT.txt) |
## BeaEngine and static linking

BeaEngine is not covered by the project's MIT license. It is distributed under
**LGPL-3.0-or-later**.

The repository currently includes BeaEngine as a static library. Distributing
a program that is statically linked with BeaEngine can require more than
including the license text. In particular, LGPL section 4 requires a compliant
way for recipients to modify or replace the LGPL-covered library and relink the
combined work. Depending on how binaries are distributed, this commonly means
providing the corresponding BeaEngine source and the application's relinkable
object files, or changing the integration to a suitable shared-library
mechanism.

The exact corresponding source for the bundled `beaengine.lib` should be
recorded and made available before publishing prebuilt binaries. Merely linking
to the current upstream repository is not a substitute for preserving the
source corresponding to the exact binary build.

## libcurl build features and transitive dependencies

The bundled `libcurl.lib` identifies itself as `7.86.0-DEV` and contains the
Windows Schannel backend. The repository also distributes zlib separately.
System libraries supplied with Windows are governed by Microsoft's terms and
are not relicensed by this repository.

If `cpr` or `libcurl` is rebuilt with a different feature set, update this
notice and include the licenses for every linked dependency. Depending on the
build, this can include OpenSSL, nghttp2, Brotli, zstd, libssh2, c-ares,
libpsl, or other libraries. Do not assume that a future `libcurl.lib` has the
same dependency set as the currently bundled artifact.

## Embedded font data

`shared/includes/sources/binaries/consola.ttf.h` appears to contain embedded
Consolas font data. Consolas is not covered by the project's MIT license, and
this repository does not include a license granting general redistribution of
that font payload.

Before public redistribution, either:

1. verify that you have an applicable redistribution right for the exact font
   data; or
2. remove the embedded payload and load a system-installed font; or
3. replace it with a font whose license expressly permits redistribution and
   include that font's license and copyright notice.

## Components not covered by this notice

Windows SDK headers, import libraries, Direct3D, DXGI, SHCore, SetupAPI, and
other operating-system components are provided under Microsoft's applicable
license terms. They are not third-party source components redistributed under
the project's MIT license.

## Maintenance rule

Whenever a bundled source tree or static library is replaced, update all of the
following together:

- the component/version table above;
- the corresponding file in `.licenses/`;
- any source-offer or relinking material required by copyleft licenses;
- the dependency and license section in `readme.md`.
