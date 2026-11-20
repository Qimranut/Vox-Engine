#include "serializer_sys.hpp"

namespace serializer
{
	void System::loadAllSaves(entt::registry& reg)
	{
		// Verify that save files exist
		bool result = util::file_io::dirExists(std::string(dir::save::files));
		if (!result)
		{
			std::cout << "No entity save files found." << std::endl;
			return;
		}

		// Load all save files found in directory
		for (const auto& entry : std::filesystem::directory_iterator(dir::save::files))
		{
			if (!entry.is_regular_file())
				continue;

			// Get filenames
			const std::string name = entry.path().filename().string();

			if (name.rfind("ecs_save_", 0) == 0)
				load(reg, std::string(dir::save::files) + name);
		}
	}

	/**
	 * Saves the ecs registry onto a JSON
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] path
	 * Save file path
	 * 
	 */
	void System::save(entt::registry& reg, const std::string& path)
	{
		// Serialize ecs registry into a JSON object
		nlohmann::json json = serializeRegistry(reg);

		// Create/open file
		std::ofstream file(path);
		if (!file.is_open())
			throw std::runtime_error("Failed to open file for writing: " + path);

		// Converts JSON object into formatted text with 4 space indentation
		file << json.dump(4);
	}

	/**
	 * Loads an ecs json entities into the ecs registry.
	 *
	 * [param] reg
	 * ECS registry
	 *
	 * [param] path
	 * Save file path
	 *
	 */
	nlohmann::json System::load(entt::registry& reg, const std::string& path)
	{
		// Create/open file
		std::ifstream file(path);
		if (!file.is_open())
			throw std::runtime_error("Failed to open file for reading: " + path);

		// Feed JSON text into JSON object
		nlohmann::json json;
		file >> json;

		// Load JSON and reconstruct entities into registry
		loadRegistry(reg, json);
		return json;
	}

	/**
	 * Registration helper for components
	 * 
	 */
	void System::registerComponents()
	{
		// Register reflected component field/enum and give the field/enum a name
#define REGISTER_FIELD(Type, field)		.data<&Type::field>(entt::hashed_string { #field }).custom<std::string_view>(std::string_view{ #field })
#define REGISTER_ENUM_VALUE(E ,v)		.data<E::v>(entt::hashed_string { #v }).custom<std::string_view>(std::string_view{ #v })

		// Registers every component under COMPONENT_LIST
#define X(Type, FIELDS) \
    entt::meta_factory<Type>{} \
        .type(entt::hashed_string{ #Type }) \
		.ctor<>() \
        .custom<std::string_view>(std::string_view{ #Type }) \
        FIELDS(Type, REGISTER_FIELD);

		COMPONENT_LIST(X)

#undef X

		// vec3 registration
		entt::meta_factory<glm::vec3>()
			.type(entt::hashed_string{ "vec3" })
			.ctor<>()
			.custom<std::string_view>(std::string_view{ "vec3" })
			GLM_VEC3_FIELD(glm::vec3, REGISTER_FIELD);

		// vec4 registration
		entt::meta_factory<glm::vec4>()
			.type(entt::hashed_string{ "vec4" })
			.ctor<>()
			.custom<std::string_view>(std::string_view{ "vec4" })
			GLM_VEC4_FIELD(glm::vec4, REGISTER_FIELD);

		// quat registration
		entt::meta_factory<glm::quat>()
			.type(entt::hashed_string{ "quat" })
			.ctor<>()
			.custom<std::string_view>(std::string_view{ "quat" })
			GLM_QUAT_FIELD(glm::quat, REGISTER_FIELD);

		// Motion type registration
		entt::meta_factory<physics::MotionType>()
			.type(entt::hashed_string{ "MotionType" })
			.custom<std::string_view>(std::string_view{ "MotionType" })
			MOTION_TYPE_VALUES(physics::MotionType, REGISTER_ENUM_VALUE);

		// Layer registration
		entt::meta_factory<physics::Layer>()
			.type(entt::hashed_string{ "Layer" })
			.custom<std::string_view>(std::string_view{ "Layer" })
			LAYER_VALUES(physics::Layer, REGISTER_ENUM_VALUE);

		// Shape type registration
		entt::meta_factory<physics::ShapeType>()
			.type(entt::hashed_string{ "ShapeType" })
			.custom<std::string_view>(std::string_view{ "ShapeType" })
			SHAPE_TYPE_VALUES(physics::ShapeType, REGISTER_ENUM_VALUE);

#undef REGISTER_FIELD
	}

	/**
	 * Converts reflected C++ object into a JSON object by converting its fields
	 * 
	 * [param] any
	 * Field
	 * 
	 * [return] 
	 * JSON object
	 * 
	 */
	nlohmann::json System::toJson(entt::meta_any any)
	{
		// Primitive cases => becomes a flat JSON value
		if (auto* v = any.try_cast<bool>())				return *v;
		if (auto* v = any.try_cast<int>())				return *v;
		if (auto* v = any.try_cast<float>())			return *v;
		if (auto* v = any.try_cast<double>())			return *v;
		if (auto* v = any.try_cast<uint32_t>())			return *v;
		if (auto* v = any.try_cast<std::string>())		return *v;

		// Non-Primitive cases
		entt::meta_type type = any.type();
		if (type.is_enum())
		{
			for (auto&& [eid, e] : type.data())
			{
				if (e.get({}) == any)
					return getName(e);
			}

			// No registered enum value matched.
			return nullptr;
		}

		nlohmann::json obj = nlohmann::json::object();

		// Iterate over every reflected field in the type
		for (auto&& [id, data] : type.data())
		{
			obj[getName(data)] = toJson(data.get(any));
		}

		return obj;
	}

	/**
	 * Converts JSON object back to a C++ object by constructing the reflected type
	 * 
	 * [param] type
	 * C++ type
	 * 
	 * [param] json
	 * JSON object
	 * 
	 * [return]
	 * Constructed component
	 * 
	 */
	entt::meta_any System::fromJson(entt::meta_type type, const nlohmann::json& json)
	{
		entt::meta_any instance = type.construct();

		if (!instance)
			throw std::runtime_error("Failed to construct reflected type.");

		// Primitive type cases
		for (auto&& [id, data] : type.data())
		{
			const std::string name = getName(data);

			if (!json.contains(name))
				continue;

			if (data.type() == entt::resolve<bool>())
			{
				data.set(instance, json[name].get<bool>());
			}
			else if (data.type() == entt::resolve<int>())
			{
				data.set(instance, json[name].get<int>());
			}
			else if (data.type() == entt::resolve<float>())
			{
				data.set(instance, json[name].get<float>());
			}
			else if (data.type() == entt::resolve<double>())
			{
				data.set(instance, json[name].get<double>());
			}
			else if (data.type() == entt::resolve<uint32_t>())
			{
				data.set(instance, json[name].get<uint32_t>());
			}
			else if (data.type() == entt::resolve<std::string>())
			{
				data.set(instance, json[name].get<std::string>());
			}
			else if (data.type().is_enum())
			{
				const std::string enumName = json[name].get<std::string>();
				for (auto&& [eid, e] : data.type().data())
				{
					if (getName(e) == enumName)
					{
						data.set(instance, e.get({}));
						break;
					}
				}
			}
			else
			{
				entt::meta_any nested = fromJson(data.type(), json[name]);

				data.set(instance, nested);
			}
		}

		return instance;
	}

	/**
	 * Saves an individual entity
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] e
	 * ECS entity
	 * 
	 * [param] path
	 * Path to save to
	 * 
	 */
	void System::saveEntity(entt::registry& reg, entt::entity e, const std::string& path)
	{
		nlohmann::json data = serializeEntity(reg, e);

		std::ofstream file(path);
		if (!file.is_open())
			throw std::runtime_error("Failed to open file for writing: " + path);

		// Converts JSON object into formatted text with 4 space indentation
		file << data.dump(4);
	}

	/**
	 * Serializes entity data to a JSON object
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] e
	 * ECS entity
	 * 
	 * [return] 
	 * JSON object
	 * 
	 */
	nlohmann::json System::serializeEntity(entt::registry& reg, entt::entity e)
	{
		nlohmann::json result = nlohmann::json::object();

		// Save entity ID
		result["entityID"] = static_cast<uint32_t>(e);

		// Iterate through component pools in the registry
		for (auto&& [id, storage] : reg.storage())
		{
			// Skip pool if entity does not have this component
			if (!storage.contains(e))
				continue;

			// storage.info() gives runtime type_info for component type held by the pool
			// Skip if component 
			entt::meta_type type = entt::resolve(storage.info());
			if (!type)
			{
				// TEMP DEBUG
				std::cerr << "Unreflected pool: " << storage.info().name()
					<< " (hash " << storage.info().hash() << ")\n";
				throw std::runtime_error("Entity component has not been reflected, but entity is currently being serialized.");
			}

			// Get entity's instance of the component
			entt::meta_any instance = type.from_void(storage.value(e));

			result[getName(type)] = toJson(instance);
		}

		return result;
	}

	/**
	 * Loads an entity's data from JSON and adds its components into the ECS registry
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] e
	 * ECS entity
	 * 
	 * [param] json
	 * JSON object
	 * 
	 */
	void System::loadEntity(entt::registry& reg, entt::entity e, const nlohmann::json& json)
	{
		for (auto&& [name, componentJson] : json.items())
		{
			// Skip ID
			if (name == "entityID")
				continue;

			entt::meta_type type = entt::resolve(entt::hashed_string{ name.c_str() });

			if (!type)
				throw std::runtime_error("Unknown component type: " + name);

			entt::meta_any component = fromJson(type, componentJson);
			if (!component)
				throw std::runtime_error("Failed to reconstruct component: " + name);

			addComponentToRegistry(
				reg,
				e,
				type,
				component);
		}
	}

	/**
	 * Serializes all entity data from the ECS registry
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [return] 
	 * JSON object
	 * 
	 */
	nlohmann::json System::serializeRegistry(entt::registry& reg)
	{
		nlohmann::json result;
		result["entities"] = nlohmann::json::array();
		
		// Iterate through the entity storage
		auto& entities = reg.storage<entt::entity>();

		for (auto e : entities)
		{
			result["entities"].push_back(serializeEntity(reg, e));
		}

		return result;
	}

	/**
	 * Reconstructs registry data from JSON object and appends them to ECS entities
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] json
	 * JSON object
	 * 
	 */
	void System::loadRegistry(entt::registry& reg, const nlohmann::json& json)
	{
		if (!json.contains("entities"))
			throw std::runtime_error("Json does not contain an entities array.");

		for (const auto& entityJson : json["entities"])
		{
			// Get original EnTT entity ID
			uint32_t id = entityJson.at("entityID").get<uint32_t>();

			// Create entity
			entt::entity e = static_cast<entt::entity>(id);
			e = reg.create(e);

			// Reconstruct its components
			loadEntity(reg, e, entityJson);
		}
	}

	/**
	 * Adds component (obtained from JSON) to the entity if
	 * it can be found in the COMPONENT_LIST
	 * 
	 * [param] reg
	 * ECS registry
	 * 
	 * [param] e
	 * ECS entity
	 * 
	 * [param] type
	 * Type read
	 * 
	 * [param] component
	 * Component to add
	 * 
	 */
	void System::addComponentToRegistry(
		entt::registry& reg, 
		entt::entity e, 
		entt::meta_type type, 
		entt::meta_any& component)
	{
		// Checks passed type with each type in COMPONENT_LIST
#define X(Type, FIELDS)											\
		if (type == entt::resolve<Type>())						\
		{														\
			addComponent<Type>(reg, e, component);				\
			return;												\
		}

		COMPONENT_LIST(X)

#undef X
			// Unknown type found
			throw std::runtime_error("Unable to add unknown component type to registry.");
	}
}
