/*************************************************************************************************************
\file       event_wiring.hpp
\par        voxEngine
\author     Tan Che Fong (chefong.tan\@digipen.edu)
\brief      Links between subsystems that no single subsystem should own. Input knows nothing of the
            camera or the character, graphics nothing of physics, and physics nothing of the camera.
            Every bus subscription that joins two of them is made here, once, from SystemManager::init().

\copyright All content 2026 DigiPen Institute of Technology Singapore. All rights reserved.
**************************************************************************************************************/

#pragma once


namespace events { class Bus; }
namespace graphics::camera { struct ControllerInput; }
namespace physics::character { struct Intent; }

namespace engine
{
	class SystemManager;
}


namespace engine::wiring
{
	/*!
		@brief
			Keeps camera intent up to date from input events.

			Exposed so the input self-check can drive it on a private bus.
		@param bus
			Bus to subscribe on
		@param out
			Intent to fill; must outlive the subscriptions
		*/
	void subscribeCameraIntent(events::Bus& bus, graphics::camera::ControllerInput& out);

	/*!
		@brief
			Keeps the player character's intent up to date from input and camera
			events - the physics counterpart of subscribeCameraIntent.
		@param bus
			Bus to subscribe on
		@param out
			Intent to fill; must outlive the subscriptions
		*/
	void subscribeCharacterIntent(events::Bus& bus, physics::character::Intent& out);

	/*!
		@brief
			Makes every engine-level bus subscription between subsystems.

			Must run before the window is created: the bus has no replay.
		@param systems
			Constructed, not yet initialised, subsystems
		*/
	void connectEvents(SystemManager& systems);

	/*!
		@brief
			Gives the orbit camera's spring arm a physics sphere cast.
		@param systems
			Subsystems with physics initialised
		*/
	void linkCameraCollision(SystemManager& systems);
}
