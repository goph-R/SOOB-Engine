# Code Restructuring + Entity Component System — Draft

## Big Picture

The engine is currently a single `main.cpp` with header-only modules (physics.h, texture.h,
obj_loader.h). Everything is hardcoded: one player, one level, no dynamic objects. To support
entities (enemies, items, decorations, platforms, etc.), we need:

1. **Code separation** — split the monolith into manageable modules
2. **Entity system** — a simple component-based architecture for game objects
3. **Blender integration** — entities placed in Blender, exported with the level

## Current Code Structure

```
main.cpp        — 612 lines: init, input, game loop, rendering, sound, cleanup
physics.h       — 146 lines: Bullet Physics world, character controller, ceiling clamp
obj_loader.h    — 285 lines: OBJ/MTL parsing, materials, sectors
texture.h       — 220 lines: BMP/TGA loading, texture cache
```

Everything lives in one flat structure. The game loop mixes input, physics, rendering, and
sound in a single while loop.

## Proposed Code Structure

```
main.cpp            — entry point, SDL init, game loop (thin orchestrator)
engine/
  renderer.h        — OpenGL setup, renderLevel, renderGun, renderCrosshair, renderEntity
  input.h           — keyboard/mouse state, event processing
  audio.h           — FMOD init, sound creation, playback helpers
  camera.h          — yaw, pitch, FPS camera logic
physics.h           — unchanged (Bullet Physics world + character)
obj_loader.h        — unchanged (OBJ/MTL parsing)
texture.h           — unchanged (BMP/TGA + cache)
iqm.h              — IQM model loader + skeletal animation (CPU skinning)
entity.h            — entity system: Entity, Component types, entity list
entity_types.h      — concrete component behaviors (enemy AI, platform movement, etc.)
level.h             — level loading: geometry + entities from file
```

The header-only style is kept (no .cpp files besides main.cpp) for Win98 build simplicity.

## Entity Component System

### Design Principles

- **Simple**: no templates, no virtual dispatch, no RTTI — C-style structs with type tags
- **C++98 compatible**: fixed arrays, no STL, no new C++ features
- **Data-driven**: entities defined in level files, not hardcoded
- **Flat**: components are inline in the entity struct, not heap-allocated

### Entity Structure

```c
#define MAX_ENTITIES 256

enum EntityType {
    ENT_NONE = 0,
    ENT_PLAYER,
    ENT_DECORATION,     /* static prop (baked into lightmap) or dynamic prop */
    ENT_ITEM,           /* pickup: health, ammo, key, etc. */
    ENT_ENEMY,          /* AI-controlled hostile */
    ENT_PLATFORM,       /* moving platform (elevator, lift) */
    ENT_SWITCH,         /* activatable: button, lever */
    ENT_TRIGGER         /* invisible volume: door trigger, zone, hurt, etc. */
};

struct Entity {
    int active;
    EntityType type;
    char name[32];          /* optional, for scripting/targeting */
    char group[32];         /* optional, for batch operations */

    /* Transform */
    float posX, posY, posZ;
    float rotY;             /* yaw rotation in degrees */
    float scaleX, scaleY, scaleZ;

    /* Visual (optional — not all entities are visible) */
    int hasMesh;
    ObjMesh mesh;           /* loaded from .obj file (static models) */
    GLuint diffuseTex;      /* texture for this entity's mesh */
    int isStatic;           /* 1 = baked into lightmap, immovable */

    /* Animated model (optional — overrides mesh if present) */
    int hasAnim;
    IqmModel *model;        /* IQM skeletal model (bones + weighted verts) */
    int currentAnim;         /* index of playing animation sequence */
    float animTime;          /* current time within the animation */
    float animSpeed;         /* playback speed multiplier, default 1.0 */

    /* Physics (optional) */
    int hasCollision;
    btRigidBody *body;          /* for dynamic/kinematic objects */
    btCollisionShape *shape;    /* collision shape */

    /* Type-specific data (union to save memory) */
    union {
        struct {            /* ENT_ITEM */
            int itemType;   /* health=0, ammo=1, key=2, etc. */
            int respawn;    /* seconds until respawn, 0=no respawn */
        } item;

        struct {            /* ENT_ENEMY */
            int health;
            int state;      /* 0=idle, 1=patrol, 2=chase, 3=attack, 4=dead */
            float speed;
            float sightRange;
            int targetEntity; /* index of current target (usually player) */
        } enemy;

        struct {            /* ENT_PLATFORM */
            float startY, endY;   /* movement range */
            float speed;
            int state;            /* 0=at start, 1=moving to end, 2=at end, 3=moving to start */
            char targetGroup[32]; /* optional: activate when triggered */
        } platform;

        struct {            /* ENT_SWITCH */
            int state;            /* 0=off, 1=on */
            char targetName[32];  /* entity name or group to activate */
        } sw;

        struct {            /* ENT_TRIGGER */
            float sizeX, sizeY, sizeZ;  /* trigger volume half-extents */
            char targetName[32];         /* what to activate on enter */
            int triggerOnce;             /* 1=disable after first trigger */
            int triggered;
        } trigger;
    };
};
```

### Entity List

```c
struct EntityList {
    Entity entities[MAX_ENTITIES];
    int count;
    int playerIndex;    /* shortcut to the player entity */
};
```

### Core Operations

```c
int    entityCreate(EntityList *list, EntityType type);  /* returns index */
void   entityDestroy(EntityList *list, int index);
Entity *entityFindByName(EntityList *list, const char *name);
void   entityActivateGroup(EntityList *list, const char *group);

void   entitiesUpdate(EntityList *list, PhysWorld *pw, float dt);  /* per-frame logic */
void   entitiesRender(EntityList *list, TexCache *cache);          /* draw all visible */
```

### Per-Type Update Logic

Each frame, `entitiesUpdate` loops through active entities and dispatches by type:

- **ENT_PLAYER**: handled by existing character controller (physics.h), entity just tracks position
- **ENT_DECORATION**: no update if static. Dynamic decorations get physics simulation.
- **ENT_ITEM**: check distance to player, pick up if close enough, play sound
- **ENT_ENEMY**: simple state machine (idle -> patrol -> chase -> attack), raycasts for sight
- **ENT_PLATFORM**: lerp between startY/endY based on state, move physics body
- **ENT_SWITCH**: check if player activates (use key), toggle state, send activation to target
- **ENT_TRIGGER**: test player overlap with AABB, fire activation once or repeatedly

### Activation / Messaging

Entities communicate through name/group targeting:
- A switch with `targetName = "door_01"` activates the entity named `door_01`
- A trigger with `targetName = "elevator_group"` activates all entities in group `elevator_group`
- Activation means different things per type: platform starts moving, switch toggles, etc.

## Blender Integration for Entities

### How Entities Are Placed

In Blender, entities are **Empty objects** or **mesh objects** placed in the scene:

- The object name becomes the entity name (e.g., `enemy_guard_01`)
- A custom property `entity_type` defines the type (e.g., `enemy`, `item`, `platform`)
- A custom property `group` defines the group (optional)
- Position/rotation/scale come from the object's transform
- If it has mesh data, it becomes the entity's visual model

### Static Decorations and Lightmaps

Static decorations (`isStatic = 1`) should be **included in the lightmap bake** because they
affect lighting (cast shadows, receive light). In Blender:

- Static decorations are regular mesh objects included in the scene during baking
- They are joined with (or placed near) the level geometry so Cycles casts their shadows
- At export time, they are separated back out as entities with `isStatic = 1`
- The engine renders them but doesn't update their physics

Dynamic decorations (pushable crates, etc.) are NOT baked — they use the level's ambient
lighting or a simplified light probe.

### Entity File Format

Entities are stored in a companion file alongside the OBJ, e.g., `test_level.ent`:

```
# Entity definitions for test_level
# Format: type name group posX posY posZ rotY [type-specific key=value pairs]

player spawn_point - 0 2 0 0
decoration barrel_01 props 5 0.5 3 0 mesh=barrel.obj tex=barrel.bmp static=1
decoration barrel_02 props 5 0.5 -3 45 mesh=barrel.obj tex=barrel.bmp static=1
item health_pack_01 - 10 2.5 0 0 item_type=0
enemy guard_01 enemies 20 2.5 -4 180 health=100 speed=3 sight=15
platform lift_01 elevator_group 20 2 4 0 start_y=2 end_y=6 speed=2
switch button_01 - 18 3 5 0 target=elevator_group
trigger zone_01 - 15 3 0 0 size=3,4,3 target=guard_01 once=1
```

### Export Workflow

The Blender export plugin (future Phase 2) will:
1. Export level geometry as OBJ+MTL (existing)
2. Scan scene for entity objects (by custom property)
3. Write `.ent` file with entity definitions
4. Copy entity mesh files (barrel.obj, etc.) to export directory

## Rendering Entities

Entity meshes are rendered after the level, using the same OpenGL pipeline:

```c
void entitiesRender(EntityList *list, TexCache *cache)
{
    for (int i = 0; i < list->count; i++) {
        Entity *e = &list->entities[i];
        if (!e->active || !e->hasMesh) continue;

        glPushMatrix();
        glTranslatef(e->posX, e->posY, e->posZ);
        glRotatef(e->rotY, 0, 1, 0);
        glScalef(e->scaleX, e->scaleY, e->scaleZ);

        if (e->diffuseTex) {
            glEnable(GL_TEXTURE_2D);
            glBindTexture(GL_TEXTURE_2D, e->diffuseTex);
        }

        /* Render entity mesh (simple single-material path) */
        glBegin(GL_TRIANGLES);
        for (int t = 0; t < e->mesh.numTris; t++) {
            /* ... vertex/normal/texcoord submission ... */
        }
        glEnd();

        if (e->diffuseTex) glDisable(GL_TEXTURE_2D);
        glPopMatrix();
    }
}
```

## Skeletal Animation (IQM Format)

### Why IQM

IQM (Inter-Quake Model) is a compact binary format designed for indie/retro game engines.
It supports skeletal animation with bones, vertex weights, and multiple named animation
sequences. The reference C loader is ~300 lines with no dependencies. A well-maintained
Blender exporter exists at https://github.com/lsalzman/iqm.

### IQM Data Model

```c
struct IqmBone {
    char name[32];
    int parent;         /* -1 for root */
    float basePos[3];   /* bind pose position */
    float baseRot[4];   /* bind pose rotation (quaternion) */
    float baseScale[3]; /* bind pose scale */
};

struct IqmVertex {
    float pos[3];
    float normal[3];
    float texcoord[2];
    unsigned char boneIndex[4];   /* up to 4 bone influences */
    unsigned char boneWeight[4];  /* weights (0-255, sum to 255) */
};

struct IqmAnim {
    char name[32];      /* e.g., "idle", "walk", "attack", "death" */
    int firstFrame;
    int numFrames;
    float framerate;
    int loop;           /* 1 = loops, 0 = plays once */
};

struct IqmModel {
    IqmVertex *verts;
    int numVerts;
    int *tris;          /* triangle indices (numTris * 3) */
    int numTris;

    IqmBone *bones;
    int numBones;

    IqmAnim *anims;
    int numAnims;

    /* Per-frame bone poses: numBones * totalFrames transforms */
    float *framePoses;  /* array of 4x3 matrices (or pos+quat+scale) */
    int totalFrames;

    /* Scratch buffer for CPU skinning (rewritten every frame) */
    float *skinnedVerts;  /* numVerts * 3 floats (transformed positions) */
    float *skinnedNorms;  /* numVerts * 3 floats (transformed normals) */

    GLuint diffuseTex;
};
```

### CPU Skinning Pipeline (per frame)

This runs on the CPU — no shaders needed, works on OpenGL 1.x:

```
1. Evaluate animation:
   - Find current frame from animTime + framerate
   - Interpolate between frame N and N+1 (lerp pos/scale, slerp rotation)
   - Compute per-bone world matrices (walk bone hierarchy, parent * local)

2. Skin vertices:
   - For each vertex, blend up to 4 bone matrices weighted by boneWeight[]
   - Transform position and normal by the blended matrix
   - Write to skinnedVerts[] / skinnedNorms[] scratch buffers

3. Render:
   - glBegin(GL_TRIANGLES)
   - Submit skinnedVerts/skinnedNorms/texcoords per triangle
   - glEnd()
```

On a Pentium 4, skinning a 300-vertex character at 30fps is trivial (~0.1ms).

### Blender Workflow

1. Model character in Blender (low-poly, 200-500 triangles for retro look)
2. Create armature (skeleton), parent mesh to armature with automatic weights
3. Create animation actions: idle, walk, run, attack, pain, death
4. Install the IQM exporter addon (from https://github.com/lsalzman/iqm)
5. Export: `File > Export > Inter-Quake Model (.iqm)`
   - Select mesh + armature
   - Check "Animations" and pick which actions to include
6. Place the `.iqm` file in the game directory

### Animation Playback in Entity System

```c
/* Set animation by name */
void entityPlayAnim(Entity *e, const char *animName, int loop);

/* Update animation (called from entitiesUpdate) */
void entityUpdateAnim(Entity *e, float dt)
{
    if (!e->hasAnim || !e->model) return;
    IqmAnim *anim = &e->model->anims[e->currentAnim];

    e->animTime += dt * e->animSpeed * anim->framerate;

    if (anim->loop) {
        /* Wrap around */
        while (e->animTime >= anim->numFrames)
            e->animTime -= anim->numFrames;
    } else {
        /* Clamp to last frame */
        if (e->animTime >= anim->numFrames - 1)
            e->animTime = (float)(anim->numFrames - 1);
    }

    iqmSkin(e->model, e->currentAnim, e->animTime);
}
```

### Animation Blending (Future)

For smooth transitions (e.g., idle -> walk), blend between two animations:
- Store `prevAnim`, `blendTime`, `blendDuration`
- During blend: skin both poses, lerp the final vertex positions
- Simple linear blend is enough for retro aesthetics

### .ent File Integration

Animated entities reference `.iqm` files instead of `.obj`:

```
enemy guard_01 enemies 20 2.5 -4 180 iqm=guard.iqm tex=guard.bmp health=100 speed=3
decoration torch_01 props 5 3 0 0 iqm=torch.iqm tex=torch.bmp static=1 anim=flicker
```

The `iqm=` key tells the loader to use IQM instead of OBJ. The `anim=` key sets the
initial animation sequence by name.

### File Structure

```
models/
  guard.iqm           — enemy model with walk/attack/death animations
  guard.bmp           — enemy texture (24-bit, small: 128x128 or 256x256)
  torch.iqm           — animated torch prop (flicker animation)
  torch.bmp           — torch texture
  barrel.obj          — static prop (no animation, use OBJ)
  barrel.bmp          — barrel texture
```

## Doors

Doors are a special entity type that combines visual mesh + physics collider + activation:

```
door door_01 - 10 0 5 0 mesh=door.obj tex=door.bmp open_dir=y open_amount=3 speed=2
```

Properties:
- `open_dir`: axis of movement (`y` = slides up, `x`/`z` = slides sideways)
- `open_amount`: how far to move (in meters)
- `speed`: open/close speed
- States: closed -> opening -> open -> closing -> closed
- Activated by switches/triggers (via name/group targeting)
- Has a Bullet rigid body collider that moves with the door

In Blender: modeled as a mesh object with the door geometry. The export plugin
detects `entity_type=door` and exports the mesh separately from the level.

## Lua Scripting (Triggers)

Lua 5.1 for custom trigger/event logic. Pure ANSI C89, ~200KB compiled, runs on Win98.

### Integration

- Vendored: `vendor/lua-5.1.5/src/` (add to build, ~20 .c files)
- Engine exposes functions to Lua: `activate(name)`, `spawn(type, ...)`,
  `play_sound(name)`, `set_anim(entity, anim)`, `get_player_pos()`, etc.
- Triggers can reference a `.lua` script instead of (or in addition to) a target:

```
trigger boss_room - 30 3 0 0 size=4,4,4 script=boss_intro.lua once=1
```

### Script Example (boss_intro.lua)

```lua
function on_enter(trigger)
    activate("boss_door_close")
    set_anim("boss_01", "roar")
    play_sound("boss_roar.wav")
    wait(2.0)
    set_anim("boss_01", "idle")
end
```

### Implementation

- `lua_bind.h`: init Lua state, register C functions, load/run scripts
- Entity trigger checks: if `script` key present, call Lua function instead of entActivate
- Keep it optional: if Lua is not compiled in, triggers fall back to name/group activation

## Blender Export Plugin (soob_export.py)

### Architecture

Single-file Blender addon (~600-800 lines Python), registers under `File > Export > SOOB Level`.

### Entity Identification in Blender

Objects are classified by a custom property `soob_type`:
- **Not set** = level geometry (joined into the OBJ)
- **`player`** = player spawn (Empty object, arrows display)
- **`decoration`** = static or animated prop
- **`enemy`** = enemy spawn point (Empty, with properties)
- **`item`** = pickup (Empty or mesh)
- **`trigger`** = invisible volume (Empty, cube display, scaled to trigger size)
- **`switch`** = activatable object (mesh)
- **`platform`** = moving platform (mesh)
- **`door`** = door (mesh)

### Custom Properties Panel

The plugin adds a panel in **Object Properties > SOOB Entity**:

```
[ SOOB Entity ]
  Type:         [Decoration ▼]      -- dropdown: player/decoration/item/enemy/...
  Name:         [barrel_01   ]      -- auto-filled from object name
  Group:        [props        ]     -- optional group for batch activation

  -- Visual --
  Model File:   [barrel.iqm   ]     -- IQM for animated, leave blank for inline OBJ
  Texture:      [barrel.bmp   ]     -- diffuse texture
  Scale:        [1.0          ]
  Flip Cull:    [ ]
  Is Static:    [✓]                 -- include in lightmap bake

  -- Trigger (visible when type=Trigger) --
  Target:       [elevator_group]
  Trigger Once: [✓]
  Script:       [boss_intro.lua]    -- optional Lua script

  -- Enemy (visible when type=Enemy) --
  Health:       [100           ]
  Speed:        [3.0           ]
  Sight Range:  [15.0          ]

  -- Platform (visible when type=Platform) --
  Start Y:      [2.0           ]
  End Y:        [6.0           ]
  Speed:        [2.0           ]

  -- Door (visible when type=Door) --
  Open Dir:     [Y ▼]
  Open Amount:  [3.0           ]
  Speed:        [2.0           ]

  -- Switch (visible when type=Switch) --
  Target:       [door_01       ]

  -- Item (visible when type=Item) --
  Item Type:    [Health ▼]          -- dropdown: health/ammo/key
```

### Viewport Overlays

The plugin registers custom draw handlers for visual feedback in the 3D viewport:

- **Player spawn**: blue crosshair icon
- **Enemies**: red wireframe capsule at entity position, cone for sight range
- **Triggers**: semi-transparent yellow box matching trigger size
- **Items**: green wireframe diamond
- **Platforms**: orange wireframe box with dashed line showing travel path
- **Doors**: purple wireframe box with arrow showing open direction
- **Switches**: cyan wireframe circle

All overlays are toggled via `View > SOOB Overlays` or a button in the N-panel.

### Export Pipeline (one-click)

`File > Export > SOOB Level (.obj)`:

**Dialog options:**
- Level name (default: scene name)
- Export path
- Lightmap resolution: 128 / 256 / 512 (default 256)
- Lightmap UV map name (default "Lightmap")
- Auto-create lightmap UVs: checkbox
- Bake lightmaps: checkbox (can disable for quick geometry-only export)
- Copy models: checkbox (copy referenced IQM/OBJ files to export dir)

**Steps:**

1. **Classify objects**:
   - No `soob_type` -> level geometry
   - With `soob_type` -> entity
   - Static decorations (`is_static=True`) -> included in lightmap bake scene

2. **Prepare level geometry**:
   - Join all geometry objects (temporarily)
   - Ensure lightmap UV map exists (auto-create with Smart UV Project if needed)

3. **Bake lightmaps** (if enabled):
   - Set render engine to Cycles
   - For each material/sector: create image, set as active, bake Diffuse (Direct+Indirect)
   - Include static decoration meshes in the bake (they cast shadows, receive light)
   - Save each as `<materialname>_lm.bmp`

4. **Export level OBJ + MTL**:
   - Standard Wavefront export with materials enabled
   - Add custom MTL comments: `# lm_map`, `# tile_scale`, `# tile_offset`

5. **Export diffuse textures**:
   - Find Image Texture nodes in each material
   - Convert to 24-bit BMP if not already

6. **Write .ent file**:
   - For each entity object, write one line with type, name, group, transform, properties
   - Transform: read from object's world position/rotation
   - Properties: read from custom properties panel

7. **Copy entity models**:
   - For each entity referencing an IQM or OBJ model file, copy it to the export directory
   - Maintain `models/` subdirectory structure

8. **Write manifest** (optional):
   - `level.json` listing all files for easy loading/packaging

### Output Structure

```
export/
  test_level.obj            -- level geometry + lightmap UVs
  test_level.mtl            -- material definitions
  test_level.ent            -- entity list
  diffuse textures:
    brick.bmp               -- tiling diffuse (shared)
    concrete.bmp
  lightmaps:
    brick_wall_lm.bmp       -- per-sector
    concrete_floor_lm.bmp
  models/
    guard.iqm               -- enemy model
    guard.bmp
    barrel.obj              -- static prop
    barrel.bmp
    door.obj                -- door mesh
    door.bmp
  scripts/
    boss_intro.lua          -- trigger scripts (if any)
```

### Installation

1. Open Blender > Edit > Preferences > Add-ons > Install
2. Select `soob_export.py`
3. Enable "SOOB Level Exporter"
4. The entity panel appears in Object Properties, export option in File > Export

## Lighting System

### Level Geometry
Already handled by baked lightmaps (no change needed).

### Entity Lighting (flat color per model)
No per-vertex normals needed. For each visible entity, sample nearby lights and
set one `glColor3f` before rendering. Half-Life 1 did the same — sample lightmap
at entity's feet, use as flat ambient.

**Light data** stored in `.ent` file:
```
dir_light ambient - 0 0 0 0 color=0.3,0.3,0.35 dir=-0.5,-1,0.3
point_light office_lamp - 20 4 0 0 color=0.6,0.6,0.8 radius=10
point_light hallway_lamp - 5 3 0 0 color=1.0,0.8,0.4 radius=8
```

**Per-entity color each frame:**
```
base = dir_light ambient color
for each point_light within radius:
    atten = 1.0 - (dist / radius)
    base += light.color * atten
glColor3f(clamp(base))
```

### Emissive Entities
For glowing objects (monitor screens, lamps): `emissive=1` property.
Skip `GL_LIGHTING` when rendering — texture color shows at full brightness.

```
decoration monitor_01 office 20 3.4 -2 0 mesh=models/monitor.obj tex=models/office.bmp static=1 emissive=1
```

### Flashlight (raycast-based, Half-Life 1 style)
1. Raycast from camera position in look direction (Bullet `rayTest`)
2. Hit point on wall = where flashlight lands
3. Pull back ~0.5m along the ray from the hit
4. Place `GL_LIGHT1` as a point light at that position
5. Toggle with `F` key

```
Player                  Light    Wall
  O ----raycast----------*--------| hit
                     (pull back)
```

No spotlight cone needed — just a point light that teleports to where you look.
Attenuation handles the natural light circle falloff. Works on both level geometry
(via GL fixed-function lighting) and entities (via the point light sampling above).

## Particle System + Dynamic Hardware Lights

### Particles (billboarded quads)

Simple CPU particles — no physics, no collision. Each particle is a camera-facing
quad rendered with additive blending.

```c
struct Particle {
    float posX, posY, posZ;
    float velX, velY, velZ;
    float r, g, b, alpha;
    float life;             /* remaining lifetime in seconds */
    float maxLife;          /* for fade calculation */
    float size;
};

#define MAX_PARTICLES 512

struct ParticleEmitter {
    float posX, posY, posZ;
    float colorR, colorG, colorB;
    float rate;             /* bursts per second */
    int burstCount;         /* particles per burst */
    float particleLife;     /* lifetime per particle */
    float particleSpeed;    /* initial velocity magnitude */
    float particleSize;
    int emitterType;        /* 0=electric, 1=fire, 2=smoke, 3=sparks */
    float timer;            /* internal timer for burst rate */
};
```

**Rendering:**
```c
glDepthMask(GL_FALSE);          /* don't write depth (transparent) */
glEnable(GL_BLEND);
glBlendFunc(GL_SRC_ALPHA, GL_ONE);  /* additive blend = glow */
glDisable(GL_LIGHTING);

for each alive particle:
    /* Billboard: extract right and up vectors from modelview matrix */
    glPushMatrix();
    glTranslatef(p.pos);
    /* Zero out rotation columns to face camera */
    glColor4f(p.r, p.g, p.b, p.alpha * (p.life / p.maxLife));
    draw_quad(p.size);
    glPopMatrix();

glDepthMask(GL_TRUE);
glDisable(GL_BLEND);
```

### Dynamic Hardware Lights

OpenGL 1.x guarantees 8 lights (GL_LIGHT0 through GL_LIGHT7).
- GL_LIGHT0: level ambient/directional (already used)
- GL_LIGHT1: flashlight (raycast-based)
- GL_LIGHT2-7: up to 6 dynamic lights for particle emitters, muzzle flash, etc.

Dynamic lights are **not baked** — they affect level geometry and entities in
real-time via the fixed-function pipeline.

**Per-emitter light:**
```c
/* Electric spark emitter: light flickers on during burst */
if (emitter.bursting) {
    glEnable(GL_LIGHT2 + lightIndex);
    float pos[] = { emitter.posX, emitter.posY, emitter.posZ, 1.0f };
    float col[] = { emitter.colorR, emitter.colorG, emitter.colorB, 1.0f };
    glLightfv(GL_LIGHT2 + lightIndex, GL_POSITION, pos);
    glLightfv(GL_LIGHT2 + lightIndex, GL_DIFFUSE, col);
    glLightf(GL_LIGHT2 + lightIndex, GL_QUADRATIC_ATTENUATION, 0.05f);
} else {
    glDisable(GL_LIGHT2 + lightIndex);
}
```

**Limit: 6 simultaneous dynamic lights.** For a retro engine with small levels,
this is plenty. If more emitters exist than available lights, prioritize by
distance to camera.

### Emitter Types

| Type | Behavior | Light |
|---|---|---|
| electric | Short bursts of bright particles, random spread | Cyan flicker |
| fire | Upward drift, orange-yellow, longer life | Warm orange steady |
| smoke | Slow upward drift, gray, large, no light | None |
| sparks | Fast downward spray, yellow-white, very short | Bright white flash |
| muzzle | Single burst at gun barrel on shoot | White flash, 1 frame |

### .ent File

```
# Particle emitters
particle sparks_01 electric_fx 10 3 5 0 type=electric color=0.5,0.8,1.0 rate=3 burst=8 speed=4 life=0.2 size=0.05 radius=5
particle torch_01 - 5 3.5 0 0 type=fire color=1.0,0.6,0.2 rate=10 burst=3 speed=1.5 life=0.8 size=0.1 radius=4
```

## Implementation Phases

### Phase B: IQM Loader + Skeletal Animation [DONE]
iqm.h: IQM v2 binary loader, bone hierarchy, CPU skinning, GL 1.x rendering.
Tested with mrfixit.iqm (1861 verts, 75 bones, idle animation).

### Phase C: Entity Core [DONE]
entity.h: Entity struct, EntityList, .ent file parser, per-type update/render.
Working: player spawn, decoration (OBJ + IQM), item pickup, trigger volumes,
platform movement, switch activation with name/group targeting.

### Phase D: Blender Export Plugin  <-- NEXT
soob_export.py: single-file addon (~600-800 lines).
- Custom properties panel for entity type/properties per object
- Viewport overlays (color-coded entity visualization)
- One-click export: OBJ+MTL + lightmap bake + .ent file + model/texture copy
- Handles static decoration lightmap inclusion
- See detailed specification above.

### Phase E: Lighting System
- Light definitions in .ent (dir_light, point_light)
- Per-entity flat color sampling from nearby lights
- Emissive flag for fullbright objects (monitor screens)
- Flashlight: raycast + pull-back point light, toggle with F key

### Phase F: Particles + Dynamic Lights
particle.h: emitter types (electric, fire, smoke, sparks), billboarded
quad rendering with additive blend, GL_LIGHT2-7 for dynamic lights
that flicker with emitter bursts. Muzzle flash becomes a particle burst.

### Phase G: Doors
Add ENT_DOOR type: mesh + collider that slides open/closed on activation.
Properties: open_dir (axis), open_amount, speed.
Bullet rigid body moves with the door for proper collision.

### Phase H: Enemy AI
Basic state machine: idle, patrol, chase, attack.
Line-of-sight raycasts via Bullet Physics.
Simple pathfinding (waypoint-based or direct chase).
Animation driven by AI state (idle->walk->attack->death).

### Phase I: Lua Scripting
Vendor Lua 5.1.5 source (~20 C files, ANSI C89, works on Win98).
lua_bind.h: init state, register engine functions, load/run trigger scripts.
Triggers can reference .lua scripts for custom sequences.

### Phase J: Code Separation
Split main.cpp into header modules (renderer.h, input.h, audio.h, camera.h).
Refactor once the feature set is more stable. No new features — just cleanup.

## Open Questions

1. **Entity mesh format**: OBJ for static props, IQM for animated models.
   Both loaders already planned. Entity decides at load time based on
   `mesh=` (OBJ) vs `iqm=` (IQM) key in .ent file.

2. **Physics for entities**: Full Bullet rigid bodies or simplified AABB checks?
   Recommendation: AABB for triggers/items (cheap), Bullet rigid bodies for
   pushable decorations and platforms (need proper collision response).

3. **Enemy pathfinding**: Waypoints or navmesh?
   Recommendation: waypoints for now — navmesh is overkill for this engine.
   Waypoints are entities too (type=waypoint, invisible).

4. **Entity limits**: MAX_ENTITIES=256 enough?
   For a retro engine with <5000 tri levels, 256 is generous.

5. **Sound per entity**: Should entities have sound components?
   Could add a simple `soundId` field for ambient/loop sounds (torch crackle, etc.).

6. **IQM vertex budget**: For Win98/P4 target, keep characters at 200-500 triangles.
   CPU skinning cost is ~0.1ms per 300-vert model on P4. With 10 visible
   animated entities, that's ~1ms total — well within budget.

7. **Animation blending**: Simple linear blend between two poses is enough
   for retro aesthetics. Implement after basic playback works.
