# Compatibility (alpha)

Results from hands-on testing on an Xbox Series S in Dev Mode. This is a snapshot, not a promise: builds change quickly. "Vulkan" is Mesa Dozen on D3D12; "OpenGL" is Mesa on D3D12.

| Game | Vulkan | OpenGL | Notes |
| --- | --- | --- | --- |
| Terraria | Plays well | Plays well | Noticeable speed gain in recent builds |
| Undertale | Runs | Runs | |
| Persona 4 Golden | Fixed in build 72 (vertex formats), needs confirming | Fixed in build 72 (render-target copy), needs confirming | Audio crackle addressed by the larger audio buffer |
| Gravity Rush | Plays | Not recommended | The crash after the opening cutscene was guarded in build 72 |
| Ultra Despair Girls | Good in gameplay | Hitches | Cutscene hitches came from shader compiling; background compiling is now the default |
| Danganronpa: Trigger Happy Havoc | Interaction bug | Interaction bug | Also open in upstream Vita3K; no known cause |
| Minecraft: PS Vita Edition | Runs slowly, some garbled text | Crashes | Use Vulkan |

Not yet tested: Uncharted: Golden Abyss.

## Reporting

Attach `vita3k-runtime.log` (or the diagnostics report from Settings), the build number, the game and its title ID, and the renderer. Report port issues in this repository, not to upstream Vita3K.
