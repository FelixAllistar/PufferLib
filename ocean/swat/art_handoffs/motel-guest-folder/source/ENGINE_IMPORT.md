# Import and placement
Metres. Root and ANCHOR_surface_contact at bottom support plane (Blender Z=0 / glTF Y=0). Root is nominal footprint center (left spine projects 0.5 mm farther). Blender +Z up, cover heading toward +Y. glTF +Y up, heading toward -Z. Preserve exported transforms and unit scale. Root-local glTF bounds: [-0.1165, 0, -0.153] to [0.116, 0.0082, 0.153]. Place on a desk or nightstand with the root at the actual support height. Keep it away from traversable edges.

Engine owns placement, support attachment, collision, gameplay semantics, interaction and disappearance with support destruction. No collision, rigid body, animation, damage fragments or executable engine changes are supplied. All four render parts belong to the same static folder. Preview support and lighting are not exported.

## Materials
Original 512 × 512 basecolor sRGB; +Y OpenGL normal linear; roughmetal linear, R=255, G=roughness, B=0 metallic. Separate baked AO 512 × 512, R, strength 1, uniquely packed UV1. Material maps use UV0. Top-cover UV0 maps readable title to the physical cover; other vinyl faces sample blank atlas margin. Paper uses restrained grain, with no fake tiny text or black line stacks. Do not merge AO into basecolor or sample it using UV0.

