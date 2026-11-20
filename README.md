# Vox Engine

C++ game engine developed as a team project for GAM300 at SIT Digipen. It was meant to be a voxel game engine, although it was scrapped part way through development.

## The ECS
I designed and implemented the ECS wrapper/layer on top of EnTT. EnTT provides the registry and component storage while I build the architecture around it to help facilitate game object creation.

Relevant Files:
- 'Engine/Source/ecs/archetype/archetype.hpp'

- 'Engine/Source/ecs/archetype_manager/archetype_mng.hpp'
- 'Engine/Source/ecs/archetype_manager/archetype_mng.cpp'

- 'Engine/Source/ecs/ecs_manager/ecs_mng.hpp'

- 'Engine/Source/components/archetype_list.hpp'
- 'Engine/Source/components/comp_field_list.hpp'
- 'Engine/Source/components/comp_list.hpp'

## What it does
- ECSManager: Entry point to the ECS. It wraps the entt::registry and exposes entity creation and destruction, as well as archetype management and registry access.
- Archetypes: Named entity templates which store a set of default component values, and entities can be instantiated with them.
- Archetype Registration: Archetypes are declared with an X-macro list (ARCHETYPE_LIST)
- Modifying an archetype by adding or removing a component will propogate these changes to every live entity instantiated from it.
- Type reflection for serialization: X-macro field lists describe each component, math type (such as GLM::vec3) and enum, to enable serialization by JSON.
- Destroyed entities will return entt::null instead of crashing the program.

### Type reflection for serialization
Each component's serializable fields are declared in an X-macro field list such as comp_field_list.hpp. For example:

```comp_field_list.hpp
#define TRANSFORM_FIELDS(Type, F) \
    F(Type, pos_)                 \
    F(Type, rot_)                 \
    F(Type, scale_)
```
The same approach is used for the other components, math types, and enums such as:
- Components: Transform, Visibility, RigidBody, Collider, RenderMaterial, CharacterBody, AudioSource, VoxelWorld, Text
- GLM vec2, GLM vec3, GLM vec4, quat
- Enums: Physics motion tpye, physics layer, and colider shape

This was implemented to allow engine users to conveniently serialize component data in one place. The generic serializer will expand each list to read and write every field to JSON, and it will only take a single line for each field.

```cpp
ecs::ECSManager ecs;
ecs.registerArchetypes();   // registers everything in ARCHETYPE_LIST

// Instantiate an entity from a registered archetype, by name
entt::entity enemy = ecs.instantiate("Enemy");

// Or create a custom archetype at runtime
ecs::Archetype& turret = ecs.createArchetype("Turret");
ecs.addArchetypeComponent(turret, Health{ 100 });

// Change the archetype: every entity created from it updates too
ecs.modifyArchetypeComponent(turret, Health{ 250 });

// Direct EnTT access is still available for systems
auto view = ecs.registry().view<Health>();

ecs.destroyEntity(enemy);
```
### Design Decisions
- By wrapping EnTT with an archetype handle, users are able to create more meaningful entities easily with less verbosity on the top level code.
- Archetypes own their defauelt data, so designers could expect changes to propagate every instance to follow the same template. 

## Building
1. Requirements: Windows, Visual Studio 2022, CMake 3.24
2. Clone the repository
3. Run build.bat
4. Open voxEngine.sln (Under the newly-generated build folder)
5. Run the solution

## Third-Party Libraries used for the engine
- EnTT
- assimp
- fmod
- freetype
- glew
- GLFW
- GLM
- Jolt
- lua
- sol2
- stb
- nlohmann/json
